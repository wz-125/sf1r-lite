#include "product_scd_receiver.h"
#include <bundles/index/IndexTaskService.h>
#include <boost/lexical_cast.hpp>
#include <boost/format.hpp>
using namespace sf1r;
namespace bfs = boost::filesystem;
ProductScdReceiver::ProductScdReceiver():index_service_(NULL)
{
    dsSyn.watchGenerate(boost::bind(&ProductScdReceiver::Run, this,_1));
}
    
void ProductScdReceiver::Run(const std::string& scd_source_dir)
{
    std::cout<<"ProductScdReceiver::Run "<<scd_source_dir<<std::endl;
    if(index_service_==NULL)
    {
        dsSyn.processed(false);
        return;
    }
    std::string index_scd_dir = index_service_->getScdDir();
    bfs::create_directories(index_scd_dir);
    //copy scds in scd_source_dir/ to index_scd_dir/
    ScdParser parser(izenelib::util::UString::UTF_8);
    static const bfs::directory_iterator kItrEnd;
    std::vector<bfs::path> scd_list;
    for (bfs::directory_iterator itr(scd_source_dir); itr != kItrEnd; ++itr)
    {
        if (bfs::is_regular_file(itr->status()))
        {
            std::string fileName = itr->path().filename();
            if (parser.checkSCDFormat(fileName) )
            {
                scd_list.push_back(itr->path());
            }
        }
    }
    if(scd_list.empty()) 
    {
        dsSyn.processed(false);
        return;
    }
    bfs::path to_dir(index_scd_dir);
    if(!CopyFileListToDir_(scd_list, to_dir))
    {
        dsSyn.processed(false);
        return;
    }
    
    //call buildCollection
    if(!index_service_->buildCollection(0))
    {
        dsSyn.processed(false);
        return;
    }
    dsSyn.processed(true);
}

bool ProductScdReceiver::CopyFileListToDir_(const std::vector<boost::filesystem::path>& file_list, const boost::filesystem::path& to_dir)
{
    std::vector<bfs::path> copied_file;
    for(uint32_t i=0;i<file_list.size();i++)
    {
        std::string to_filename = file_list[i].filename();
        bool failed = true;
        while(true)
        {
            bfs::path to_file = to_dir/to_filename;
            try
            {
                if(!bfs::exists(to_file))
                {
                    bfs::copy_file(file_list[i], to_file);
                    failed = false;
                }
            }
            catch(std::exception& ex)
            {
                
            }
            if(!failed)
            {
                break;
            }
            if(!NextScdFileName_(to_filename)) break;
        }
        if(failed) //rollback
        {
            for(uint32_t j=0;j<copied_file.size();j++)
            {
                bfs::remove_all(copied_file[j]);
            }
            return false;
        }
        else
        {
            bfs::path to_file = to_dir/to_filename;
            copied_file.push_back(to_file);
        }
    }
    return true;
}

bool ProductScdReceiver::NextScdFileName_(std::string& filename) const
{
    std::string scid = filename.substr(2,2);
    uint32_t cid = boost::lexical_cast<uint32_t>(scid);
    if(cid==99) return false;
    ++cid;
    std::string ofilename = filename;
    filename = ofilename.substr(0,2);
    boost::format formater("%02d");
    formater % cid;
    filename += formater.str();
    filename += ofilename.substr(4, ofilename.length()-4);
    return true;
}


