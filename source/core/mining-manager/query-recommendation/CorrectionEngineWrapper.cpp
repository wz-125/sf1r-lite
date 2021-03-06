#include "CorrectionEngineWrapper.h"
#include "pinyin/PinYin.h"
#include "Rebuilder.h"
#include <sys/inotify.h>

namespace sf1r
{

std::string CorrectionEngineWrapper::system_resource_path_;
std::string CorrectionEngineWrapper::system_working_path_;

CorrectionEngineWrapper::CorrectionEngineWrapper()
    : engine_(NULL)
{
    engine_ = new Recommend::CorrectionEngine(system_working_path_ + "/query_correction/");
    engine_->setPinYinConverter(Recommend::PinYin::QueryCorrection::getInstance().getPinYinConverter());
    engine_->setPinYinApproximator(Recommend::PinYin::QueryCorrection::getInstance().getPinYinApproximator());
    if (engine_->isNeedBuild(system_resource_path_ + "/query_correction/"))
        engine_->buildEngine(system_resource_path_ + "/query_correction/");
    
    static Recommend::RebuildHanlder handler = boost::bind(&CorrectionEngineWrapper::rebuild, this);
    Rebuilder::get()->addRebuildHanlder(&handler);
    Rebuilder::get()->start(system_resource_path_ + "/query_correction/");
}

CorrectionEngineWrapper:: ~CorrectionEngineWrapper()
{
    if (NULL != engine_)
    {
        delete engine_;
        engine_ = NULL;
    }
}

void CorrectionEngineWrapper::rebuild()
{
    boost::unique_lock<boost::shared_mutex> ul(mtx_);
    if (engine_->isNeedBuild(system_resource_path_ + "/query_correction/"))
        engine_->buildEngine(system_resource_path_ + "/query_correction/");
}

CorrectionEngineWrapper& CorrectionEngineWrapper::getInstance()
{
    static CorrectionEngineWrapper wrapper;
    return wrapper;
}

bool CorrectionEngineWrapper::correct(const std::string& userQuery, std::string& results, double& freq )
{
    boost::shared_lock<boost::shared_mutex> sl(mtx_, boost::try_to_lock);

    if (!sl)
        return false;

    if (NULL != engine_)
        return engine_->correct(userQuery, results, freq);
    return false;
}
    
void CorrectionEngineWrapper::evaluate(std::string& stream)
{
    boost::shared_lock<boost::shared_mutex> sl(mtx_, boost::try_to_lock);
    if (!sl)
        return ;

    if (NULL != engine_)
        engine_->evaluate(system_resource_path_ + "/query_correction/evaluate/", stream);
}

}
