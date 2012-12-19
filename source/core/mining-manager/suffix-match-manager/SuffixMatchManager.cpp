#include "SuffixMatchManager.hpp"
#include <document-manager/DocumentManager.h>
#include <boost/filesystem.hpp>
#include <glog/logging.h>
#include <icma/icma.h>
#include <la-manager/LAPool.h>
#include <mining-manager/util/split_ustr.h>
#include <mining-manager/group-manager/DateStrFormat.h>
#include "FilterManager.h"
#include "FMIndexManager.h"

using namespace cma;

namespace
{
const char* NUMERIC_RANGE_DELIMITER = "-";

typedef std::pair<int64_t, int64_t> NumericRange;

using namespace sf1r::faceted;

/**
 * check parameter of group label
 * @param[in] labelParam parameter to check
 * @param[out] isRange true for group on range, false for group on single numeric value
 * @return true for success, false for failure
*/
bool checkLabelParam(const GroupParam::GroupLabelParam& labelParam, bool& isRange)
{
    const GroupParam::GroupPathVec& labelPaths = labelParam.second;

    if (labelPaths.empty())
        return false;

    const GroupParam::GroupPath& path = labelPaths[0];
    if (path.empty())
        return false;

    const std::string& propValue = path[0];
    std::size_t delimitPos = propValue.find(NUMERIC_RANGE_DELIMITER);
    isRange = (delimitPos != std::string::npos);

    return true;
}

bool convertNumericLabel(const std::string& src, float& target)
{
    std::size_t delimitPos = src.find(NUMERIC_RANGE_DELIMITER);
    if (delimitPos != std::string::npos)
    {
        LOG(ERROR) << "group label parameter: " << src
                   << ", it should be specified as single numeric value";
        return false;
    }

    try
    {
        target = boost::lexical_cast<float>(src);
    }
    catch(const boost::bad_lexical_cast& e)
    {
        LOG(ERROR) << "failed in casting label from " << src
                   << " to numeric value, exception: " << e.what();
        return false;
    }

    return true;
}

bool convertRangeLabel(const std::string& src, NumericRange& target)
{
    std::size_t delimitPos = src.find(NUMERIC_RANGE_DELIMITER);
    if (delimitPos == std::string::npos)
    {
        LOG(ERROR) << "group label parameter: " << src
                   << ", it should be specified as numeric range value";
        return false;
    }

    int64_t lowerBound = std::numeric_limits<int64_t>::min();
    int64_t upperBound = std::numeric_limits<int64_t>::max();

    try
    {
        if (delimitPos)
        {
            std::string sub = src.substr(0, delimitPos);
            lowerBound = boost::lexical_cast<int64_t>(sub);
        }

        if (delimitPos+1 != src.size())
        {
            std::string sub = src.substr(delimitPos+1);
            upperBound = boost::lexical_cast<int64_t>(sub);
        }
    }
    catch(const boost::bad_lexical_cast& e)
    {
        LOG(ERROR) << "failed in casting label from " << src
                    << " to numeric value, exception: " << e.what();
        return false;
    }

    target.first = lowerBound;
    target.second = upperBound;

    return true;
}

}


namespace sf1r
{
using namespace faceted;

SuffixMatchManager::SuffixMatchManager(
        const std::string& homePath,
        const std::string& dicpath,
        boost::shared_ptr<DocumentManager>& document_manager,
        faceted::GroupManager* groupmanager,
        faceted::AttrManager* attrmanager,
        NumericPropertyTableBuilder* numeric_tablebuilder)
    : data_root_path_(homePath)
    , tokenize_dicpath_(dicpath)
    , document_manager_(document_manager)
    , analyzer_(NULL)
    , knowledge_(NULL)
    , suffixMatchTask_(NULL)
{
    if (!boost::filesystem::exists(homePath))
    {
        boost::filesystem::create_directories(homePath);
    }
    buildTokenizeDic();

    filter_manager_.reset(new FilterManager(groupmanager, data_root_path_,
            attrmanager, numeric_tablebuilder));
    fmi_manager_.reset(new FMIndexManager(data_root_path_, document_manager_, filter_manager_));
}

SuffixMatchManager::~SuffixMatchManager()
{
    if (analyzer_) delete analyzer_;
    if (knowledge_) delete knowledge_;
}

void SuffixMatchManager::addFMIndexProperties(const std::vector<std::string>& property_list, int type, bool finished)
{
    fmi_manager_->addProperties(property_list, (FMIndexManager::PropertyFMType)type);
    if (finished)
    {
        if (!fmi_manager_->loadAll())
        {
            LOG(ERROR) << "load fm index data error!";
        }
    }
}

bool SuffixMatchManager::isStartFromLocalFM() const
{
    return fmi_manager_ && fmi_manager_->isStartFromLocalFM();
}

size_t SuffixMatchManager::longestSuffixMatch(
        const izenelib::util::UString& pattern,
        std::vector<std::string> search_in_properties,
        size_t max_docs,
        std::vector<std::pair<double, uint32_t> >& res_list) const
{
    if (!fmi_manager_) return 0;

    std::map<uint32_t, double> res_list_map;
    std::vector<uint32_t> docid_list;
    std::vector<size_t> doclen_list;
    size_t max_match;
    size_t total_match = 0;

    for (size_t i = 0; i < search_in_properties.size(); ++i)
    {
        const std::string& property = search_in_properties[i];
        FMIndexManager::RangeListT match_ranges;
        max_match = 0;
        {
            ReadLock lock(mutex_);
            max_match = fmi_manager_->longestSuffixMatch(property, pattern, match_ranges);
            if (max_match == 0)
                continue;
            LOG(INFO) << "longestSuffixMatch on property: " << property <<
                ", length: " << max_match;
            for (size_t i = 0; i < match_ranges.size(); ++i)
            {
                LOG(INFO) << "range " << i << ": " << match_ranges[i].first << "-" << match_ranges[i].second;
            }
            std::vector<double> max_match_list(match_ranges.size(), max_match);
            fmi_manager_->convertMatchRanges(property, max_docs, match_ranges, max_match_list);
            fmi_manager_->getMatchedDocIdList(property, match_ranges, max_docs, docid_list, doclen_list);
        }

        for (size_t j = 0; j < docid_list.size(); ++j)
        {
            double score = 0;
            if (doclen_list[j] > 0)
                score = double(max_match) / double(doclen_list[j]);
            std::map<uint32_t, double>::iterator res_it = res_list_map.find(docid_list[j]);
            if (res_it != res_list_map.end())
            {
                res_it->second += score;
            }
            else
            {
                res_list_map[docid_list[i]] = score;
            }
        }

        for (size_t j = 0; j < match_ranges.size(); ++j)
        {
            total_match += match_ranges[i].second - match_ranges[i].first;
        }
        std::vector<uint32_t>().swap(docid_list);
        std::vector<size_t>().swap(doclen_list);
    }
    res_list.reserve(res_list_map.size());
    for (std::map<uint32_t, double>::const_iterator cit = res_list_map.begin();
        cit != res_list_map.end(); ++cit)
    {
        res_list.push_back(std::make_pair(cit->second, cit->first));
    }
    std::sort(res_list.begin(), res_list.end(), std::greater<std::pair<double, uint32_t> >());
    if (res_list.size() > max_docs)
        res_list.erase(res_list.begin() + max_docs, res_list.end());
    return total_match;
}

size_t SuffixMatchManager::AllPossibleSuffixMatch(
        const izenelib::util::UString& pattern_orig,
        std::vector<std::string> search_in_properties,
        size_t max_docs,
        const SearchingMode::SuffixMatchFilterMode& filter_mode,
        const std::vector<QueryFiltering::FilteringType>& filter_param,
        const GroupParam& group_param,
        std::vector<std::pair<double, uint32_t> >& res_list) const
{
    if (!analyzer_) return 0;

    std::map<uint32_t, double> res_list_map;
    std::vector<std::pair<size_t, size_t> > match_ranges_list;
    std::vector<std::pair<double, uint32_t> > single_res_list;
    std::vector<double> max_match_list;

    // tokenize the pattern.
    izenelib::util::UString pattern = pattern_orig;
    Algorithm<UString>::to_lower(pattern);
    string pattern_str;
    pattern.convertString(pattern_str, UString::UTF_8);
    LOG(INFO) << "original query string: " << pattern_str;

    Sentence pattern_sentence(pattern_str.c_str());
    analyzer_->runWithSentence(pattern_sentence);
    std::vector<UString> all_sub_strpatterns;
    LOG(INFO) << "query tokenize by maxprefix match in dictionary: ";
    for (int i = 0; i < pattern_sentence.getCount(0); i++)
    {
        printf("%s, ", pattern_sentence.getLexicon(0, i));
        all_sub_strpatterns.push_back(UString(pattern_sentence.getLexicon(0, i), UString::UTF_8));
    }
    cout << endl;

    size_t match_dic_pattern_num = all_sub_strpatterns.size();

    LOG(INFO) << "query tokenize by maxprefix match in bigram: ";
    for (int i = 0; i < pattern_sentence.getCount(1); i++)
    {
        printf("%s, ", pattern_sentence.getLexicon(1, i));
        all_sub_strpatterns.push_back(UString(pattern_sentence.getLexicon(1, i), UString::UTF_8));
    }
    cout << endl;

    std::vector<size_t> prop_id_list;
    std::vector<RangeListT> filter_range_list;
    if (!group_param.isGroupEmpty())
    {
        if (!getAllFilterRangeFromGroupLable_(group_param, prop_id_list, filter_range_list))
            return 0;
    }
    if (!group_param.isAttrEmpty())
    {
        if (!getAllFilterRangeFromAttrLable_(group_param, prop_id_list, filter_range_list))
            return 0;
    }
    if (!filter_param.empty())
    {
        if (!getAllFilterRangeFromFilterParam_(filter_param, prop_id_list, filter_range_list))
            return 0;
    }

    size_t total_match = 0;
    for (size_t prop_i = 0; prop_i < search_in_properties.size(); ++prop_i)
    {
        const std::string& search_property = search_in_properties[prop_i];
        match_ranges_list.reserve(all_sub_strpatterns.size());
        max_match_list.reserve(all_sub_strpatterns.size());
        ReadLock lock(mutex_);
        LOG(INFO) << "query tokenize match ranges in property : " << search_property;
        for (size_t i = 0; i < all_sub_strpatterns.size(); ++i)
        {
            if (all_sub_strpatterns[i].empty())
                continue;
            std::pair<size_t, size_t> sub_match_range;
            size_t matched = fmi_manager_->backwardSearch(search_property, all_sub_strpatterns[i], sub_match_range);
            LOG(INFO) << "match length: " << matched << ", range:" << sub_match_range.first << "," << sub_match_range.second << endl;
            if (matched == all_sub_strpatterns[i].length())
            {
                match_ranges_list.push_back(sub_match_range);
                if (i < match_dic_pattern_num)
                    max_match_list.push_back((double)2.0);
                else
                    max_match_list.push_back((double)1.0);
            }
        }
        fmi_manager_->convertMatchRanges(search_property, max_docs, match_ranges_list, max_match_list);
        if (filter_mode == SearchingMode::OR_Filter)
        {
            fmi_manager_->getTopKDocIdListByFilter(
                search_property,
                prop_id_list,
                filter_range_list,
                match_ranges_list,
                max_match_list,
                max_docs,
                single_res_list);
        }
        else if (filter_mode == SearchingMode::AND_Filter)
        {
            // TODO: implement it when fm-index is ready for AND filter.
            assert(false);
        }
        else
        {
            LOG(ERROR) << "unknown filter mode.";
        }
        LOG(INFO) << "topk finished in property : " << search_property;
        size_t oldsize = res_list_map.size();
        for (size_t i = 0; i < single_res_list.size(); ++i)
        {
            std::map<uint32_t, double>::iterator res_it = res_list_map.find(single_res_list[i].second);
            if (res_it != res_list_map.end())
            {
                res_it->second += single_res_list[i].first;
            }
            else
            {
                res_list_map[single_res_list[i].second] = single_res_list[i].first;
            }
        }
        std::vector<std::pair<double, uint32_t> >().swap(single_res_list);

        for (size_t i = 0; i < match_ranges_list.size(); ++i)
        {
            total_match += match_ranges_list[i].second - match_ranges_list[i].first;
        }
        std::vector<std::pair<size_t, size_t> >().swap(match_ranges_list);
        std::vector<double>().swap(max_match_list);
        LOG(INFO) << "new added docid number: " << res_list_map.size() - oldsize;
    }

    res_list.reserve(res_list_map.size());
    for (std::map<uint32_t, double>::const_iterator cit = res_list_map.begin();
        cit != res_list_map.end(); ++cit)
    {
        res_list.push_back(std::make_pair(cit->second, cit->first));
    }
    std::sort(res_list.begin(), res_list.end(), std::greater<std::pair<double, uint32_t> >());
    if (res_list.size() > max_docs)
        res_list.erase(res_list.begin() + max_docs, res_list.end());
    LOG(INFO) << "all property fuzzy search finished";
    return total_match;
}

bool SuffixMatchManager::getAllFilterRangeFromAttrLable_(
        const GroupParam& group_param,
        std::vector<size_t>& prop_id_list,
        std::vector<RangeListT>& filter_range_list) const
{
    if (!filter_manager_)
    {
        LOG(INFO) << "no filter support.";
        return false;
    }

    size_t prop_id = filter_manager_->getAttrPropertyId();
    if (prop_id == (size_t)-1) return true;

    FMIndexManager::FilterRangeT filter_range;
    FilterManager::FilterIdRange filterid_range;
    std::vector<FMIndexManager::FilterRangeT> tmp_filter_ranges;
    RangeListT temp_range_list;
    for (GroupParam::AttrLabelMap::const_iterator cit = group_param.attrLabels_.begin();
            cit != group_param.attrLabels_.end(); ++cit)
    {
        const std::vector<std::string>& attr_values = cit->second;
        for (size_t i = 0; i < attr_values.size(); ++i)
        {
            const UString& attr_filterstr = filter_manager_->formatAttrPath(UString(cit->first, UString::UTF_8),
                    UString(attr_values[i], UString::UTF_8));
            filterid_range = filter_manager_->getStrFilterIdRangeExact(prop_id, attr_filterstr);
            if (filterid_range.start >= filterid_range.end)
            {
                LOG(WARNING) << "attribute filter id range not found. " << attr_filterstr;
                continue;
            }
            if (!fmi_manager_->getFilterRange(prop_id, std::make_pair(filterid_range.start, filterid_range.end), filter_range))
            {
                LOG(WARNING) << "get filter DocArray range failed.";
                continue;
            }

            LOG(INFO) << "attribute filter DocArray range is : " << filter_range.first << ", " << filter_range.second;
            temp_range_list.push_back(filter_range);
        }
    }

    if (!temp_range_list.empty())
    {
        prop_id_list.push_back(prop_id);
        filter_range_list.push_back(temp_range_list);
    }

    return true;
}

bool SuffixMatchManager::getAllFilterRangeFromGroupLable_(
        const GroupParam& group_param,
        std::vector<size_t>& prop_id_list,
        std::vector<RangeListT>& filter_range_list) const
{
    if (!filter_manager_)
    {
        LOG(INFO) << "no filter support.";
        return false;
    }

    FMIndexManager::FilterRangeT filter_range;
    FilterManager::FilterIdRange filterid_range;

    for (GroupParam::GroupLabelMap::const_iterator cit = group_param.groupLabels_.begin();
            cit != group_param.groupLabels_.end(); ++cit)
    {
        bool is_numeric = filter_manager_->isNumericProp(cit->first);
        bool is_date = filter_manager_->isDateProp(cit->first);
        const GroupParam::GroupPathVec& group_values = cit->second;

        size_t prop_id = filter_manager_->getPropertyId(cit->first);
        if (prop_id == (size_t)-1) continue;

        RangeListT temp_range_list;
        for (size_t i = 0; i < group_values.size(); ++i)
        {
            if (is_numeric)
            {
                bool is_range = false;
                if (!checkLabelParam(*cit, is_range))
                {
                    continue;
                }
                if (is_range)
                {
                    NumericRange range;
                    if (!convertRangeLabel(group_values[i][0], range))
                        continue;
                    FilterManager::FilterIdRange tmp_range;
                    tmp_range = filter_manager_->getNumFilterIdRangeLess(prop_id, std::max(range.first, range.second), true);
                    filterid_range = filter_manager_->getNumFilterIdRangeGreater(prop_id, std::min(range.first, range.second), true);
                    filterid_range.start = std::max(filterid_range.start, tmp_range.start);
                    filterid_range.end = std::min(filterid_range.end, tmp_range.end);
                }
                else
                {
                    float value = 0;
                    if (!convertNumericLabel(group_values[i][0], value))
                        continue;
                    filterid_range = filter_manager_->getNumFilterIdRangeExact(prop_id, value);
                }
            }
            else if (is_date)
            {
                const std::string& propValue = group_values[i][0];
                faceted::DateStrFormat dsf;
                faceted::DateStrFormat::DateVec date_vec;
                if (!dsf.apiDateStrToDateVec(propValue, date_vec))
                {
                    LOG(WARNING) << "get date from datestr error. " << propValue;
                    continue;
                }
                double value = (double)dsf.createDate(date_vec);
                LOG(INFO) << "date value is : " << value;
                int date_index = date_vec.size() - 1;
                if (date_index < faceted::DateStrFormat::DATE_INDEX_DAY)
                {
                    faceted::DateStrFormat::DateVec end_date_vec = date_vec;
                    // convert to range.
                    if (date_index == faceted::DateStrFormat::DATE_INDEX_MONTH)
                    {
                        end_date_vec.push_back(31);
                    }
                    else if (date_index == faceted::DateStrFormat::DATE_INDEX_YEAR)
                    {
                        end_date_vec.push_back(12);
                        end_date_vec.push_back(31);
                    }
                    else
                    {
                        continue;
                    }
                    double end_value = (double)dsf.createDate(end_date_vec);
                    LOG(INFO) << "end date value is : " << end_value;
                    FilterManager::FilterIdRange tmp_range;
                    tmp_range = filter_manager_->getNumFilterIdRangeLess(prop_id, end_value, true);
                    filterid_range = filter_manager_->getNumFilterIdRangeGreater(prop_id, value, true);
                    filterid_range.start = std::max(filterid_range.start, tmp_range.start);
                    filterid_range.end = std::min(filterid_range.end, tmp_range.end);
                }
                else
                {
                    filterid_range = filter_manager_->getNumFilterIdRangeExact(prop_id, value);
                }
            }
            else
            {
                const UString& group_filterstr = filter_manager_->formatGroupPath(group_values[i]);
                filterid_range = filter_manager_->getStrFilterIdRangeExact(prop_id, group_filterstr);
            }

            if (filterid_range.start >= filterid_range.end)
            {
                LOG(WARNING) << "one of group label filter id range not found.";
                continue;
            }
            if (!fmi_manager_->getFilterRange(prop_id, std::make_pair(filterid_range.start, filterid_range.end), filter_range))
            {
                LOG(WARNING) << "get filter DocArray range failed.";
                continue;
            }

            temp_range_list.push_back(filter_range);
            LOG(INFO) << "group label filter DocArray range is : " << filter_range.first << ", " << filter_range.second;
        }

        if (!temp_range_list.empty())
        {
            prop_id_list.push_back(prop_id);
            filter_range_list.push_back(temp_range_list);
        }
    }

    return true;
}

bool SuffixMatchManager::getAllFilterRangeFromFilterParam_(
        const std::vector<QueryFiltering::FilteringType>& filter_param,
        std::vector<size_t>& prop_id_list,
        std::vector<RangeListT>& filter_range_list) const
{
    if (!filter_manager_)
    {
        LOG(INFO) << "no filter support.";
        return false;
    }

    FMIndexManager::FilterRangeT filter_range;
    FilterManager::FilterIdRange filterid_range;

    for (size_t i = 0; i < filter_param.size(); ++i)
    {
        const QueryFiltering::FilteringType& filtertype = filter_param[i];

        bool is_numeric = filter_manager_->isNumericProp(filtertype.property_)
            || filter_manager_->isDateProp(filtertype.property_);
        size_t prop_id = filter_manager_->getPropertyId(filtertype.property_);
        if (prop_id == (size_t)-1) continue;

        RangeListT temp_range_list;
        for (size_t j = 0; j < filtertype.values_.size(); ++j)
        {
            try
            {
                if (is_numeric)
                {
                    double filter_num = filtertype.values_[j].getNumber();
                    LOG(INFO) << "filter num by : " << filter_num;

                    switch (filtertype.operation_)
                    {
                    case QueryFiltering::LESS_THAN_EQUAL:
                        filterid_range = filter_manager_->getNumFilterIdRangeLess(prop_id, filter_num, true);
                        break;

                    case QueryFiltering::LESS_THAN:
                        filterid_range = filter_manager_->getNumFilterIdRangeLess(prop_id, filter_num, false);
                        break;

                    case QueryFiltering::GREATER_THAN_EQUAL:
                        filterid_range = filter_manager_->getNumFilterIdRangeGreater(prop_id, filter_num, true);
                        break;

                    case QueryFiltering::GREATER_THAN:
                        filterid_range = filter_manager_->getNumFilterIdRangeGreater(prop_id, filter_num, false);
                        break;

                    case QueryFiltering::RANGE:
                        {
                            assert(filtertype.values_.size() == 2);
                            if (j >= 1) continue;
                            double filter_num_2 = filtertype.values_[1].getNumber();
                            FilterManager::FilterIdRange tmp_range;
                            tmp_range = filter_manager_->getNumFilterIdRangeLess(prop_id, std::max(filter_num, filter_num_2), true);
                            filterid_range = filter_manager_->getNumFilterIdRangeGreater(prop_id, std::min(filter_num, filter_num_2), true);
                            filterid_range.start = std::max(filterid_range.start, tmp_range.start);
                            filterid_range.end = std::min(filterid_range.end, tmp_range.end);
                        }
                        break;

                    case QueryFiltering::EQUAL:
                        filterid_range = filter_manager_->getNumFilterIdRangeExact(prop_id, filter_num);
                        break;

                    default:
                        LOG(WARNING) << "not support filter operation for numeric property in fuzzy searching.";
                        continue;
                    }
                }
                else
                {
                    const std::string& filter_str = filtertype.values_[j].get<std::string>();
                    LOG(INFO) << "filter range by : " << filter_str;

                    switch (filtertype.operation_)
                    {
                    case QueryFiltering::EQUAL:
                        filterid_range = filter_manager_->getStrFilterIdRangeExact(prop_id, UString(filter_str, UString::UTF_8));
                        break;

                    case QueryFiltering::GREATER_THAN:
                        filterid_range = filter_manager_->getStrFilterIdRangeGreater(prop_id, UString(filter_str, UString::UTF_8), false);
                        break;

                    case QueryFiltering::GREATER_THAN_EQUAL:
                        filterid_range = filter_manager_->getStrFilterIdRangeGreater(prop_id, UString(filter_str, UString::UTF_8), true);
                        break;

                    case QueryFiltering::LESS_THAN:
                        filterid_range = filter_manager_->getStrFilterIdRangeLess(prop_id, UString(filter_str, UString::UTF_8), false);
                        break;

                    case QueryFiltering::LESS_THAN_EQUAL:
                        filterid_range = filter_manager_->getStrFilterIdRangeLess(prop_id, UString(filter_str, UString::UTF_8), true);
                        break;

                    case QueryFiltering::RANGE:
                        {
                            assert(filtertype.values_.size() == 2);
                            if (j >= 1) continue;
                            const std::string& filter_str1 = filtertype.values_[1].get<std::string>();
                            FilterManager::FilterIdRange tmp_range;
                            if (filter_str < filter_str1)
                            {
                                tmp_range = filter_manager_->getStrFilterIdRangeLess(prop_id, UString(filter_str1, UString::UTF_8), true);
                                filterid_range = filter_manager_->getStrFilterIdRangeGreater(prop_id, UString(filter_str, UString::UTF_8), true);
                            }
                            else
                            {
                                tmp_range = filter_manager_->getStrFilterIdRangeLess(prop_id, UString(filter_str, UString::UTF_8), true);
                                filterid_range = filter_manager_->getStrFilterIdRangeGreater(prop_id, UString(filter_str1, UString::UTF_8), true);
                            }
                            filterid_range.start = std::max(filterid_range.start, tmp_range.start);
                            filterid_range.end = std::min(filterid_range.end, tmp_range.end);
                        }
                        break;

                    case QueryFiltering::PREFIX:
                        filterid_range = filter_manager_->getStrFilterIdRangePrefix(prop_id, UString(filter_str, UString::UTF_8));
                        break;

                    default:
                        LOG(WARNING) << "not support filter operation for string property in fuzzy searching.";
                        continue;
                    }
                }
            }
            catch (const boost::bad_get &)
            {
                LOG(INFO) << "get filter string failed. boost::bad_get.";
                continue;
            }

            if (filterid_range.start >= filterid_range.end)
            {
                LOG(WARNING) << "filter id range not found. ";
                continue;
            }

            if (!fmi_manager_->getFilterRange(prop_id, std::make_pair(filterid_range.start, filterid_range.end), filter_range))
            {
                LOG(WARNING) << "get filter DocArray range failed.";
                continue;
            }

            temp_range_list.push_back(filter_range);
            LOG(INFO) << "filter range is : " << filterid_range.start << ", " << filterid_range.end;
            LOG(INFO) << "filter DocArray range is : " << filter_range.first << ", " << filter_range.second;
        }

        if (!temp_range_list.empty())
        {
            prop_id_list.push_back(prop_id);
            filter_range_list.push_back(temp_range_list);
        }
    }
    return true;
}

bool SuffixMatchManager::buildMiningTask()
{
    suffixMatchTask_ = new SuffixMatchMiningTask(
            document_manager_,
            fmi_manager_,
            filter_manager_,
            data_root_path_);

    if (!suffixMatchTask_)
    {
        LOG(INFO) << "Build SuffixMatch MingTask ERROR";
        return false;
    }
    return false;
}

MiningTask* SuffixMatchManager::getMiningTask()
{
    if (suffixMatchTask_)
    {
        return suffixMatchTask_;
    }
    return NULL;
}

boost::shared_ptr<FilterManager>& SuffixMatchManager::getFilterManager()
{
    return filter_manager_;
}

void SuffixMatchManager::buildTokenizeDic()
{
    std::string cma_path;
    LAPool::getInstance()->get_cma_path(cma_path);
    boost::filesystem::path cma_fmindex_dic(cma_path);
    cma_fmindex_dic /= boost::filesystem::path(tokenize_dicpath_);
    LOG(INFO) << "fm-index dictionary path : " << cma_fmindex_dic.c_str() << endl;
    knowledge_ = CMA_Factory::instance()->createKnowledge();
    knowledge_->loadModel( "utf8", cma_fmindex_dic.c_str(), false);
    analyzer_ = CMA_Factory::instance()->createAnalyzer();
    analyzer_->setOption(Analyzer::OPTION_TYPE_POS_TAGGING, 0);
    // using the maxprefix analyzer
    analyzer_->setOption(Analyzer::OPTION_ANALYSIS_TYPE, 100);
    analyzer_->setKnowledge(knowledge_);
    LOG(INFO) << "load dictionary knowledge finished." << endl;
}

}