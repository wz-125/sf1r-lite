#include <search-manager/NumericPropertyTable.h>
#include <util/ustring/UString.h>
#include <cstring>
#include "NumericRangeGroupCounter.h"

NS_FACETED_BEGIN

const int NumericRangeGroupCounter::bound_[]
    = {0, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000};

NumericRangeGroupCounter::NumericRangeGroupCounter(const NumericPropertyTable *propertyTable)
    : propertyTable_(propertyTable)
{
    segmentTree_.resize(100 * LEVEL_1_OF_SEGMENT_TREE);
}

NumericRangeGroupCounter::~NumericRangeGroupCounter()
{
    delete propertyTable_;
}

void NumericRangeGroupCounter::addDoc(docid_t doc)
{
    int value;
    if (propertyTable_->convertPropertyValue(doc, value))
    {
        int exponent;
        for (exponent = 1; value >= bound_[exponent]; ++exponent);
        value /= bound_[exponent--] / 100;
        ++segmentTree_[100 * exponent + value];
    }
}

void NumericRangeGroupCounter::getGroupRep(OntologyRep& groupRep) const
{
    int level0 = 0;
    int level1[LEVEL_1_OF_SEGMENT_TREE];
    int level2[LEVEL_1_OF_SEGMENT_TREE][10];
    int split[LEVEL_1_OF_SEGMENT_TREE];
    int rangeNumber = MAX_RANGE_NUMBER;

    std::vector<int>::const_iterator it = segmentTree_.begin();

    for (int i = 0; i < LEVEL_1_OF_SEGMENT_TREE; i++)
    {
        level1[i] = 0;
        for (int j = 0; j < 10; j++)
        {
            level2[i][j] = 0;
            for (int k = 0; k < 10; k++)
            {
                level2[i][j] += *(it++);
            }
            level1[i] += level2[i][j];
        }
        level0 += level1[i];
        if (level1[i])
        {
            split[i] = 1;
            --rangeNumber;
        }
        else
            split[i] = 0;
    }

    while (rangeNumber > 0)
    {
        int best = -1;
        double bestVarianceReduce;

        for (int i = 0; i < LEVEL_1_OF_SEGMENT_TREE; i++)
        {
            if (level1[i] <= split[i])
                continue;

            double varianceReduce = (double) level1[i] * level1[i] / split[i] / (split[i] + 1);

            if (best == -1 || varianceReduce > bestVarianceReduce)
            {
                best = i;
                bestVarianceReduce = varianceReduce;
            }
        }
        if (best != -1)
        {
            ++split[best];
            --rangeNumber;
        }
        else break;
    }

    std::list<OntologyRepItem>& itemList = groupRep.item_list;

    izenelib::util::UString propName(propertyTable_->getPropertyName(), UString::UTF_8);
    itemList.push_back(faceted::OntologyRepItem(0, propName, 0, level0));

    for (int i = 0; i < LEVEL_1_OF_SEGMENT_TREE; i++)
    {
        std::vector<int>::const_iterator bit = segmentTree_.begin() + 100 * i;
        if (!split[i])
            continue;

        std::pair<int, int> stop(0, 0), end(9, 9);
        int tempCount = 0, oldCount = 0;
        int atom = bound_[i + 1] / 100;

        while (level2[i][end.first] == 0)
            --end.first;

        it = bit + 10 * end.first + 9;
        while (*it == 0)
        {
            --end.second;
            --it;
        }

        while (level2[i][stop.first] == 0)
            ++stop.first;

        it = bit + 10 * stop.first;
        while (*it == 0)
        {
            ++stop.second;
            ++it;
        }

        for (int j = 0; j < split[i] - 1; j++)
        {
            int difference = tempCount - ((j + 1) * level1[i] - 1) / split[i] - 1;
            std::pair<int, int> tempStop, oldStop(stop);

            while (true)
            {
                int newDifference = difference + *it;
                if (newDifference + difference >= 0)
                    break;

                if (newDifference != difference)
                {
                    tempCount += *it;
                    difference = newDifference;
                    tempStop = stop;
                }
                if (tempCount == level1[i])
                    break;

                while (++stop.second < 10 && *(++it) == 0);
                if (stop.second == 10)
                {
                    while (level2[i][++stop.first] == 0);
                    stop.second = 0;
                    it = bit + 10 * stop.first;
                    while (*it == 0)
                    {
                        ++stop.second;
                        ++it;
                    }
                }
            }

            if (oldCount != tempCount)
            {
                itemList.push_back(faceted::OntologyRepItem());
                faceted::OntologyRepItem& repItem = itemList.back();
                repItem.level = 1;
                std::stringstream ss;
                ss << atom * (10 * oldStop.first + oldStop.second)
                    << "-"
                    << atom * (10 * tempStop.first + tempStop.second + 1) - 1;

                izenelib::util::UString stringValue(ss.str(), UString::UTF_8);
                repItem.text = stringValue;
                repItem.doc_count = tempCount - oldCount;
                if (tempCount == level1[i])
                    break;

                oldCount = tempCount;
            }
        }

        if (tempCount == level1[i])
            continue;

        itemList.push_back(faceted::OntologyRepItem());
        faceted::OntologyRepItem& repItem = itemList.back();
        repItem.level = 1;
        std::stringstream ss;
        ss << atom * (10 * stop.first + stop.second)
            << "-"
            << atom * (10 * end.first + end.second + 1) - 1;

        izenelib::util::UString stringValue(ss.str(), UString::UTF_8);
        repItem.text = stringValue;
        repItem.doc_count = level1[i] - tempCount;
    }
}

NS_FACETED_END
