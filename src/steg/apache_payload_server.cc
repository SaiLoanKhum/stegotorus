#include <fstream>
#include <sstream>
#include <vector>
#include <boost/filesystem.hpp>

using namespace std;
using namespace boost::filesystem;

#include "util.h"
#include "curl_util.h"
#include "crypt.h"
#include "rng.h"
#include "apache_payload_server.h"
#include "payload_scraper.h"

/**
  The constructor reads the payload database prepared by scraper
  and initialize the payload table.
*/
ApachePayloadServer::ApachePayloadServer(MachineSide init_side, string database_filename)
  :PayloadServer(init_side),_database_filename(database_filename),
   _apache_host_name("127.0.0.1"),
   c_max_buffer_size(1000000),
   c_ACCEPTABLE_EFFICIENCY_COMPROMISE(30)

{
  /* Ideally this should check the side and on client side
     it should not attempt openning the the database file but
     for now we keep it for testing */
  
  //(_side == server_side) {
  /* First we read all the payload info from the db file  */
  ifstream payload_info_stream;


  if (_side == server_side) {
    //Initializing type specific data, we initiate with max_capacity = 0, count = 0
    _payload_database.type_detail[HTTP_CONTENT_JAVASCRIPT] =  TypeDetail(0, 0);
    _payload_database.type_detail[HTTP_CONTENT_HTML] =  TypeDetail(0, 0);

    _payload_database.type_detail[HTTP_CONTENT_PDF] =  TypeDetail(0, 0);

    _payload_database.type_detail[HTTP_CONTENT_SWF] = TypeDetail(0, 0);

    if (!boost::filesystem::exists(_database_filename))
      {
        log_debug("payload database does not exists.");
        log_debug("scarping payloads to create the database...");

        PayloadScraper my_scraper(_database_filename);
        my_scraper.scrape();

      }
    
    payload_info_stream.open(_database_filename, ifstream::in);
    if (!payload_info_stream.is_open())
        {
          log_abort("Cannot open payload info file.");
        }
      
    unsigned long file_id;
    while (payload_info_stream >> file_id) {
    
      PayloadInfo cur_payload_info;
      string cur_hash;

      payload_info_stream >>  cur_payload_info.type;
      payload_info_stream >>  cur_hash;
      payload_info_stream >>  cur_payload_info.capacity;
      payload_info_stream >>  cur_payload_info.length;
      payload_info_stream >>  cur_payload_info.url;

      _payload_database.payloads.insert(pair<string, PayloadInfo>(cur_hash, cur_payload_info));
      _payload_database.sorted_payloads.push_back(EfficiencyIndicator(cur_hash, cur_payload_info.length));
                                                  
      //update type related global data 
      _payload_database.type_detail[cur_payload_info.type].count++;
      if (cur_payload_info.capacity > _payload_database.type_detail[cur_payload_info.type].max_capacity)
        _payload_database.type_detail[cur_payload_info.type].max_capacity = cur_payload_info.capacity;

    } // while
     
    if (payload_info_stream.bad())
      log_abort("payload info file corrupted.");
        
    _payload_database.sorted_payloads.sort();
    
    log_debug("loaded %ld payloads from %s\n", _payload_database.payloads.size(), _database_filename.c_str());
    
    //This is how server side initiates the uri dict
    init_uri_dict();
  }
  else{ //client side
    payload_info_stream.open(_database_filename, ifstream::in);
    if (!(payload_info_stream.is_open())) //on client side it is not a fatal error
      log_debug("payload info file doesn't exists. I need to request it from server ");
    else {
      if (!init_uri_dict(payload_info_stream))
        log_debug("payload info file is corrupted. I need to request it from server ");
      payload_info_stream.close();
    }

  }

    
  //init curl
  if (!(_curl_obj = curl_easy_init()))
    log_abort("Failed to initiate the curl object");

  curl_easy_setopt(_curl_obj, CURLOPT_HEADER, 1L);
  curl_easy_setopt(_curl_obj, CURLOPT_HTTP_CONTENT_DECODING, 0L);
  curl_easy_setopt(_curl_obj, CURLOPT_HTTP_TRANSFER_DECODING, 0L);
  curl_easy_setopt(_curl_obj, CURLOPT_WRITEFUNCTION, curl_read_data_cb);

}

unsigned int
ApachePayloadServer::find_client_payload(char* buf, int len, int type)
{
  (void)buf; 
  (void)len;
  (void)type;

  //TODO to be implemented 
  return 0;
}

int
ApachePayloadServer::get_payload( int contentType, int cap, char** buf, int* size)
{
  int found = 0, numCandidate = 0;

  //log_debug("contentType = %d, initTypePayload = %d, typePayloadCount = %d",
  //            contentType, pl.initTypePayload[contentType],
  //          pl.typePayloadCount[contentType]);

  //get payload is not supposed to act like this but for the sake 
  //of testing and compatibility we are simulating the original 
  //get_payload
  PayloadInfo* itr_first, *itr_best = NULL;
  list<EfficiencyIndicator>::iterator  itr_payloads = _payload_database.sorted_payloads.begin();

  PayloadInfo* cur_payload_candidate = &_payload_database.payloads[itr_payloads->url_hash];
  while(itr_payloads != _payload_database.sorted_payloads.end() && (cur_payload_candidate->capacity < (unsigned int)cap ||
                                                                   cur_payload_candidate->type != (unsigned int)contentType)){
    itr_payloads++; numCandidate++;
    cur_payload_candidate = &_payload_database.payloads[itr_payloads->url_hash];
  }

  if (itr_payloads != _payload_database.sorted_payloads.end() && cur_payload_candidate->length < c_max_buffer_size)
    {
      found = true;
      itr_first = itr_best = cur_payload_candidate;
    }
  // while(numCandidate < MAX_CANDIDATE_PAYLOADS) {
  //   //itr_payloads = _payload_database.payloads.begin();
  //   //advance(itr_payloads, rng_int(_payload_database.payloads.size()));  
    
  //     //if ((*itr_payloads).second.capacity <= (unsigned int)cap || (*itr_payloads).second.type != (unsigned int)contentType || (*itr_payloads).second.length > c_max_buffer_size || (*itr_payloads).second.length/c_ACCEPTABLE_EFFICIENCY_COMPROMISE > (unsigned int)cap )
  //   if (cur_payload_candidate->capacity <= (unsigned int)cap) //the list is sorted so we'll have no more luck
  //     break; 
  //   if (cur_payload_candidate->type != (unsigned int)contentType || cur_payload_candidate->length > c_max_buffer_sizep)
  //     {
  //       itr_payloads++;
  //       continue;
  //     }

  //   found = true;
  //   itr_first = cur_payload_candidate;
  //   numCandidate++;

  //   if (itr_best == NULL)
  //     itr_best = cur_payload_candidate;
  //   else if (itr_best->length > cur_payload_candidate->length)
  //     itr_best = cur_payload_candidate;

  //   if (cur_payload_candidate->length/c_ACCEPTABLE_EFFICIENCY_COMPROMISE < (unsigned int)cap)
  //     break;

  //   itr_payloads++;

  // }

  if (found)
    {
      log_debug("cur payload size=%d, best payload size=%d, num candidate=%d for transmiting %d bytes\n",
                itr_first->length,
                itr_best->length,
                numCandidate,
                cap);

      if (!itr_best->cached) {
          stringstream tmp_stream_buf;
          string payload_uri = "http://" + _apache_host_name + "/" + (*itr_best).url;

          itr_best->cached_size = fetch_url_raw(_curl_obj, payload_uri, tmp_stream_buf);
          if (itr_best->cached_size == 0)
            {
              log_abort("Failed fetch the url %s", (*itr_best).url.c_str());
              return 0;
            }

          itr_best->cached = new char[itr_best->cached_size];
          tmp_stream_buf.read(itr_best->cached, itr_best->cached_size);
      }
      
      *buf = itr_best->cached;
      *size = itr_best->cached_size;
      return 1;
      
    } 
  
  /*not found*/
  return 0;
}


bool
ApachePayloadServer::init_uri_dict()
{
  if (_payload_database.payloads.size() == 0)
    {
      log_debug("Payload database is empty or not initialized.");
      return false;
    }

  uri_dict.clear();
  uri_decode_book.clear();

  PayloadDict::iterator itr_payloads;
  unsigned long i = 0;

  for (itr_payloads = _payload_database.payloads.begin(); itr_payloads != _payload_database.payloads.end(); itr_payloads++, i++) {

    uri_dict.push_back(URIEntry((*itr_payloads).second.url));
    uri_decode_book[itr_payloads->second.url] = i;
  }

  compute_uri_dict_mac();
  return true;

}

bool
ApachePayloadServer::init_uri_dict(istream& dict_stream)
{
  uri_dict.clear();
  uri_decode_book.clear();

  string cur_url;
  for (size_t i = 0; dict_stream >> cur_url; i++) {
    uri_dict.push_back(URIEntry(cur_url));
    uri_decode_book[cur_url] = i;

  }

  log_debug("Stored uri dictionary loaded with %lu entries", uri_dict.size());

  compute_uri_dict_mac();
  if (!dict_stream.bad()) 
    return true;

  log_debug("corrupted dictionary buffer");
  return false;
  
}

void
ApachePayloadServer::export_dict(iostream& dict_stream)
{
  URIDict::iterator itr_uri;
  for(itr_uri = uri_dict.begin(); itr_uri != uri_dict.end(); itr_uri++)
    {
      dict_stream << itr_uri->URL.c_str() << endl;
    }
  
}

const uint8_t*
ApachePayloadServer::compute_uri_dict_mac()
{
  stringstream dict_str_stream;
  export_dict(dict_str_stream);
  
  sha256((const uint8_t*)dict_str_stream.str().c_str(), dict_str_stream.str().size(), _uri_dict_mac);

  return _uri_dict_mac;

}

bool
ApachePayloadServer::store_dict(char* dict_buf, size_t dict_buf_size)
{

  ofstream dict_file(_database_filename);

  if (!dict_file.is_open()){
    log_warn("error in openning file:%s to store the uri dict: %s", _database_filename.c_str(), strerror(errno));
    return false;
  }

  dict_file.write(dict_buf, dict_buf_size);
  if (dict_file.bad()){
    log_warn("error in storing the uri dict: %s",strerror(errno));
    dict_file.close();
    return false;
  }

  dict_file.close();
  return true;
}

ApachePayloadServer::~ApachePayloadServer()
{
    /* always cleanup */ 
    curl_easy_cleanup(_curl_obj);

}

int 
ApachePayloadServer::find_url_type(const char* uri)
{

  const char* ext = strrchr(uri, '.');

  if (ext == NULL || !strncmp(ext, ".html", 5) || !strncmp(ext, ".htm", 4) || !strncmp(ext, ".php", 4)
      || !strncmp(ext, ".jsp", 4) || !strncmp(ext, ".asp", 4))
    return HTTP_CONTENT_HTML;

  if (!strncmp(ext, ".js", 3) || !strncmp(ext, ".JS", 3))
    return HTTP_CONTENT_JAVASCRIPT;

  if (!strncmp(ext, ".pdf", 4) || !strncmp(ext, ".PDF", 4))
    return HTTP_CONTENT_PDF;


  if (!strncmp(ext, ".swf", 4) || !strncmp(ext, ".SWF", 4))
    return HTTP_CONTENT_SWF;

  return 0;
}
