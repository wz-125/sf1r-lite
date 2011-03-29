///
/// @file TaxonomyGenerationSubManager.h
/// @brief Provide taxonomy generation algorithm.
/// @author Jinglei Zhao <zhao.jinglei@gmail.com>
/// @date Created 2009-05-18
/// @date Updated 2009-11-21 00:59:16  Jia Guo <guojia@gmail.com>
///

#ifndef TAXONOMYGENERATIONSUBMANAGER_H_
#define TAXONOMYGENERATIONSUBMANAGER_H_


#include "TgTypes.h"
#include "TaxonomyRep.h"
#include <mining-manager/MiningManagerDef.h>
#include <common/PropertyTermInfo.h>
#include <configuration-manager/MiningConfig.h>
#include <string>
#include <vector>
#include <boost/shared_ptr.hpp>

#include <am/cccr_hash/cccr_hash.h>
#include <am/tokyo_cabinet/tc_hash.h>
#include <common/type_defs.h>
#include "LabelManager.h"
#include <am/3rdparty/rde_hash.h>
#include <idmlib/util/idm_analyzer.h>
#include <idmlib/concept-clustering/algorithm.hpp>

namespace sf1r{
    
class TaxonomyGenerationSubManager : public boost::noncopyable {


    
public:
    TaxonomyGenerationSubManager(
            const TaxonomyPara& taxonomy_param,
            const boost::shared_ptr<LabelManager>& labelManager,
            idmlib::util::IDMAnalyzer* analyzer);
    ~TaxonomyGenerationSubManager();
    
public:
    /// @brief Given a query, get the query specific taxonomy information to display.
    /// 
    /// @param docIdList The top doc id list to be taxonomy.
    /// @param queryTermIdList The query term id list, used for label ranking.
    /// @param docQueryPosition Query term position in doc id list.
    /// @param idManager The idManager, to convert id to ustring.
    /// @param taxonomyRep The output parameter.
    /// 
    /// @return If succ.
    bool getQuerySpecificTaxonomyInfo(
            const std::vector<docid_t>& docIdList,
            const izenelib::util::UString& queryStr,
            uint32_t totalCount,
            uint32_t numberFromSia,
            TaxonomyRep& taxonomyRep
            ,ne_result_list_type& neList);

    
private:
    
    void getNEList_(std::vector<std::pair<labelid_t, docid_t> >& inputPairList
    , const std::vector<uint32_t>& docIdList, uint32_t totalDocCount,uint32_t max, std::vector<ne_item_type >& neList);

private:
    TaxonomyPara taxonomy_param_;
    std::string path_;
    boost::shared_ptr<LabelManager> labelManager_;
    idmlib::util::IDMAnalyzer* analyzer_;
    idmlib::cc::Algorithm<LabelManager> algorithm_;
    idmlib::cc::Parameters tgParams_;
//     idmlib::cc::Parameters peopParams_;
//     idmlib::cc::Parameters locParams_;
//     idmlib::cc::Parameters orgParams_;

    static const unsigned int default_levels_=3;
    static const unsigned int default_perLevelNum_=8;
    static const unsigned int default_candLabelNum_=250;

        
};
}

#endif /* TAXONOMYGENERATIONSUBMANAGER_H_ */