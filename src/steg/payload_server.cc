/* Copyright 2011, 2012 SRI International
 * Copryight 2012, vmon
 * See LICENSE for other credits and copying information
 */

#include "util.h"
#include "payload_server.h"
#include "file_steg.h"
#include "http_steg_mods/swfSteg.h"
#include "http_steg_mods/pdfSteg.h"
//#include "http_steg_mods/jsSteg.h"
#include <ctype.h>
#include <time.h>

const vector<uint8_t> PayloadServer::c_empty_payload;

bool operator< (const EfficiencyIndicator &lhs, const EfficiencyIndicator& rhs) {
    return (lhs.length < rhs.length);
}
    
unsigned int
PayloadServer::capacityPDF (char* buf, int len) {
  char *hEnd, *bp, *streamStart, *streamEnd;
  int cnt=0;
  int size;

  // jump to the beginning of the body of the HTTP message
  hEnd = strstr(buf, "\r\n\r\n");
  if (hEnd == NULL) {
    // cannot find the separator between HTTP header and HTTP body
    return 0;
  }
  bp = hEnd + 4;

  while (bp < (buf+len)) {
     streamStart = strInBinary("stream", 6, bp, (buf+len)-bp);
     if (streamStart == NULL) break;
     bp = streamStart+6;
     streamEnd = strInBinary("endstream", 9, bp, (buf+len)-bp);
     if (streamEnd == NULL) break;
     // count the number of char between streamStart+6 and streamEnd
     size = streamEnd - (streamStart+6) - 2; // 2 for \r\n before streamEnd
     if (size > 0) {
       cnt = cnt + size;
       //log_debug("capacity of pdf increase by %d", size);
     }
     bp += 9;
  }
  return cnt;
}


/*
 * capacitySWF is just mock function 
  returning the len just for the sake of harmonizing
  the capacity computation. We need to make payload
  types as a children of all classes.
 */
unsigned int 
PayloadServer::capacitySWF(char* buf, int len)
{
  (void)buf;
  return len;
}

/*
 * capacityJS is designed to call capacityJS3 
 */
// unsigned int 
// PayloadServer::capacityJS (char* buf, int len) {

//   int mode = has_eligible_HTTP_content(buf, len, HTTP_CONTENT_JAVASCRIPT);
//   if (mode != CONTENT_JAVASCRIPT)
//     mode = has_eligible_HTTP_content(buf, len, HTTP_CONTENT_HTML);
  
//   if (mode != CONTENT_HTML_JAVASCRIPT && mode != CONTENT_JAVASCRIPT)
//     return 0;

//   size_t cap = capacityJS3(buf, len, mode);

//   if (cap <  JS_DELIMITER_SIZE)
//     return 0;
    
//   return (cap - JS_DELIMITER_SIZE)/2;
// }


/* first line is of the form....
   GET /XX/XXXX.swf[?YYYY] HTTP/1.1\r\n
*/


int 
PayloadServer::find_uri_type(const char* buf_orig, int buflen) {

  std::string buf(buf_orig, buflen);
  buf[buflen] = 0; //make it a null terminated buffer for sake of strchr

  if (strncmp(buf.c_str(), "GET", 3) != 0
      && strncmp(buf.c_str(), "POST", 4) != 0) {
    log_debug("Unable to determine URI type. Not a GET/POST requests.\n");
    return -1;
  }

  size_t uri_pos = buf.find(' ');

  if (uri_pos == std::string::npos) {
    log_debug("Invalid URL\n");
    return -1;
  }

  uri_pos += 1;

  size_t uri_end_pos = buf.find('?', uri_pos);
  if (uri_end_pos == std::string::npos)
    uri_end_pos = buf.find(' ', uri_pos);
  
  if (uri_end_pos == std::string::npos) {
    log_debug("unterminated uri: %s", buf.substr(uri_pos).c_str());
    return -1;
  }

  size_t filename_pos = buf.rfind('/', uri_end_pos);

  if (filename_pos == std::string::npos) {
    log_debug("no / in url: %s", buf.substr(uri_pos, uri_end_pos - uri_pos + 1).c_str());
    return -1;
  }

  std::string filename = buf.substr(filename_pos, uri_end_pos - filename_pos); //we don't want to include
  //the uri end character ' ' or '?' in the filename
  size_t ext_pos = filename.find('.');
  //if an extension is found then there is a dot otherwise it is null
  log_debug("payload extension is %s", filename.substr(ext_pos+1).c_str());
  return (ext_pos == std::string::npos) ?
    extension_to_content_type(NULL) :    
    extension_to_content_type(filename.substr(ext_pos+1).c_str());

}

/**
   get the file extension and return the numerical contstant representing the content type

   @param extension file extension such as html, htm, js, jpg, 

   @return content type constant or -1 if not found, a null extensions is considered as html type
*/
int 
PayloadServer::extension_to_content_type(const char* extension) {

  if (extension == NULL || !strncmp(extension, "html", 5) || !strncmp(extension, "htm", 4) || !strncmp(extension, "php", 4)
      || !strncmp(extension, "jsp", 4) || !strncmp(extension, "asp", 4))
    return HTTP_CONTENT_HTML;

  if (!strncmp(extension, "js", 3) || !strncmp(extension, "JS", 3))
    return HTTP_CONTENT_JAVASCRIPT;

  if (!strncmp(extension, "pdf", 4) || !strncmp(extension, "PDF", 4))
    return HTTP_CONTENT_PDF;

  if (!strncmp(extension, "swf", 4) || !strncmp(extension, "SWF", 4))
    return HTTP_CONTENT_SWF;

  if (!strncmp(extension, "png", 4) || !strncmp(extension, "PNG", 4))
    return HTTP_CONTENT_PNG;

  if (!strncmp(extension, "jpg", 4) || !strncmp(extension, "JPG", 4))
    return HTTP_CONTENT_JPEG;

  if (!strncmp(extension, "gif", 4) || !strncmp(extension, "GIF", 4))
    return HTTP_CONTENT_GIF;

  return -1;
  
}


/**
   set the set of active type whose corresponding steg mode are permitted to use 
   this is mostly for testing specific steg types

   @param active_steg_mod_list comma separated string set of active steg mod indicated by extension. currently 
   only one active steg is supported

   @return true if successful false if there was a problem with the indicated type.
*/
bool PayloadServer::set_active_steg_mods(const std::string& active_steg_mod_list)
{
  //TODO: only one steg extension is accepted here. multiple steg type
  //activation should be supported.
  int requested_steg_type =  extension_to_content_type(active_steg_mod_list.c_str());

  if (requested_steg_type == -1)
    return false;
  
  active_steg_mods.push_back(requested_steg_type);

  return true;

}

void
gen_rfc_1123_date(char* buf, int buf_size) {
  time_t t = time(NULL);
  struct tm *my_tm = gmtime(&t);
  strftime(buf, buf_size, "Date: %a, %d %b %Y %H:%M:%S GMT\r\n", my_tm);
}

void
gen_rfc_1123_expiry_date(char* buf, int buf_size) {
  time_t t = time(NULL) + rand() % 10000;
  struct tm *my_tm = gmtime(&t);
  strftime(buf, buf_size, "Expires: %a, %d %b %Y %H:%M:%S GMT\r\n", my_tm);
}

int
gen_response_header(char* content_type, int gzip, int length, char* buf, int buflen) {
  char* ptr;

  // conservative assumption here.... 
  if (buflen < 400) {
    fprintf(stderr, "gen_response_header: buflen too small\n");
    return -1;
  }

  sprintf(buf, "HTTP/1.1 200 OK\r\n");
  ptr = buf + strlen("HTTP/1.1 200 OK\r\n");
  gen_rfc_1123_date(ptr, buflen - (ptr - buf));
  ptr = ptr + strlen(ptr);

  sprintf(ptr, "Server: Apache\r\n");
  ptr = ptr + strlen(ptr);

  switch(rand() % 9) {
  case 1:
    sprintf(ptr, "Vary: Cookie\r\n");
    ptr = ptr + strlen(ptr);
    break;

  case 2:
    sprintf(ptr, "Vary: Accept-Encoding, User-Agent\r\n");
    ptr = ptr + strlen(ptr);
    break;

  case 3:
    sprintf(ptr, "Vary: *\r\n");
    ptr = ptr + strlen(ptr);
    break;

  }


  switch(rand() % 4) {
  case 2:
    gen_rfc_1123_expiry_date(ptr, buflen - (ptr - buf));
    ptr = ptr + strlen(ptr);
  }
 

  if (gzip) 
    sprintf(ptr, "Content-Length: %d\r\nContent-Encoding: gzip\r\nContent-Type: %s\r\n", length, content_type);
  else
    sprintf(ptr, "Content-Length: %d\r\nContent-Type: %s\r\n", length, content_type);
    
  ptr += strlen(ptr);

  switch(rand() % 4) {
  case 2:
  case 3:
  case 4:
    sprintf(ptr, "Connection: Keep-Alive\r\n\r\n");
    break;
  default:
    sprintf(ptr, "Connection: close\r\n\r\n");
    break;    
  }

  ptr += strlen(ptr);

  return ptr - buf;
}






int
PayloadServer::parse_client_headers(char* inbuf, char* outbuf, int len) {
  // client-side
  // remove Host: field
  // remove referrer fields?

  char* ptr = inbuf;
  int outlen = 0;

  while (1) {
    // char* end = strstr(ptr, "\r\n", len - (ptr - inbuf));
    char* end = strstr(ptr, "\r\n");
    if (end == NULL) {
      fprintf(stderr, "invalid client header %d %d %s \n PTR = %s\n", len, (int) (len - (ptr - inbuf)), inbuf, ptr);
      // fprintf(stderr, "HERE %s\n", ptr);
      break;
    }

    if (!strncmp(ptr, "Host:", 5) ||
	!strncmp(ptr, "Referer:", 8) ||
	!strncmp(ptr, "Cookie:", 7)) {
      goto next;
    }

    memcpy(outbuf + outlen, ptr, end - ptr + 2);
    outlen += end - ptr + 2;

  next:
    if (!strncmp(end, "\r\n\r\n", 4)){
      break;
    }
    ptr = end+2;
  }
  
  return outlen;

  // server-side
  // fix date fields
  // fix content-length



}

/*
 * has_eligible_HTTP_content() identifies if the input HTTP message 
 * contains a specified type of content, used by a steg module to
 * select candidate HTTP message as cover traffic
 */

// for JavaScript, there are two cases:
// 1) If Content-Type: has one of the following values
//       text/javascript 
//       application/x-javascript
//       application/javascript
// 2) Content-Type: text/html and 
//    HTTP body contains <script type="text/javascript"> ... </script>
// #define CONTENT_JAVASCRIPT		1 (for case 1)
// #define CONTENT_HTML_JAVASCRIPT	2 (for case 2)
//
// for pdf, we look for the msgs whose Content-Type: has one of the
// following values
// 1) application/pdf
// 2) application/x-pdf
// 

int 
PayloadServer::has_eligible_HTTP_content (char* buf, int len, int type) {
  char* ptr = buf;
  char* matchptr;
  int tjFlag=0, thFlag=0, ceFlag=0, teFlag=0, http304Flag=0, clZeroFlag=0, pdfFlag=0, swfFlag=0; //, gzipFlag=0; // compiler under Ubuntu complains about unused vars, so commenting out until we need it
  char* end, *cp;

#ifdef DEBUG
  fprintf(stderr, "TESTING availabilty of js in payload ... \n");
#endif

  if (type != HTTP_CONTENT_JAVASCRIPT &&
      type != HTTP_CONTENT_HTML &&
      type != HTTP_CONTENT_PDF && type != HTTP_CONTENT_SWF)
    return 0;

  // assumption: buf is null-terminated
  if (!strstr(buf, "\r\n\r\n"))
    return 0;


  while (1) {
    end = strstr(ptr, "\r\n");
    if (end == NULL) {
      break;
    }

    if (!strncmp(ptr, "Content-Type:", 13)) {
	
      if (!strncmp(ptr+14, "text/javascript", 15) || 
	  !strncmp(ptr+14, "application/javascript", 22) || 
	  !strncmp(ptr+14, "application/x-javascript", 24)) {
	tjFlag = 1;
      }
      if (!strncmp(ptr+14, "text/html", 9)) {
	thFlag = 1;
      }
      if (!strncmp(ptr+14, "application/pdf", 15) || 
	  !strncmp(ptr+14, "application/x-pdf", 17)) {
	pdfFlag = 1;
      }
      if (!strncmp(ptr+14, "application/x-shockwave-flash", strlen("application/x-shockwave-flash"))) {
	swfFlag = 1;
      }

    } else if (!strncmp(ptr, "Content-Encoding: gzip", 22)) {
      //      gzipFlag = 1; // commented out as variable is set but never read and Ubuntu compiler complains
    } else if (!strncmp(ptr, "Content-Encoding:", 17)) { // Content-Encoding that is not gzip
      ceFlag = 1;
    } else if (!strncmp(ptr, "Transfer-Encoding:", 18)) {
      teFlag = 1;
    } else if (!strncmp(ptr, "HTTP/1.1 304 ", 13)) {
      http304Flag = 1;
    } else if (!strncmp(ptr, "Content-Length: 0", 17)) {
      clZeroFlag = 1;
    }
    
    if (!strncmp(end, "\r\n\r\n", 4)){
      break;
    }
    ptr = end+2;
  }

#ifdef DEBUG
  printf("tjFlag=%d; thFlag=%d; gzipFlag=%d; ceFlag=%d; teFlag=%d; http304Flag=%d; clZeroFlag=%d\n", 
    tjFlag, thFlag, gzipFlag, ceFlag, teFlag, http304Flag, clZeroFlag);
#endif

  // if (type == HTTP_CONTENT_JAVASCRIPT)
  if (type == HTTP_CONTENT_JAVASCRIPT || type == HTTP_CONTENT_HTML) {
    // empty body if it's HTTP not modified (304) or zero Content-Length
    if (http304Flag || clZeroFlag) return 0; 

    // for now, we're not dealing with Transfer-Encoding (e.g., chunked)
    // or Content-Encoding that is not gzip
    // if (teFlag) return 0;
    if (teFlag || ceFlag) return 0;

    if (tjFlag && ceFlag && end != NULL) {
      log_debug("(JS) gzip flag detected with hdr len %d", (int)(end-buf+4));
    } else if (thFlag && ceFlag && end != NULL) {
      log_debug("(HTML) gzip flag detected with hdr len %d", (int)(end-buf+4));
    }

    // case 1
    if (tjFlag) return 1; 

    // case 2: check if HTTP body contains <script type="text/javascript">
    if (thFlag) {
      matchptr = strstr(ptr, "<script type=\"text/javascript\">");
      if (matchptr != NULL) {
        return 2;
      }
    }
  }

  if (type == HTTP_CONTENT_PDF && pdfFlag) {
    // reject msg with empty body: HTTP not modified (304) or zero Content-Length
    if (http304Flag || clZeroFlag) return 0; 

    // for now, we're not dealing with Transfer-Encoding (e.g., chunked)
    // or Content-Encoding that is not gzip
    // if (teFlag) return 0;
    if (teFlag || ceFlag) return 0;

    // check if HTTP body contains "endstream";
    // strlen("endstream") == 9
    
    cp = strInBinary("endstream", 9, ptr, buf+len-ptr);
    if (cp != NULL) {
      // log_debug("Matched endstream!");
      return 1;
    }
  }
  
  //check if we need to update this for current SWF implementation
  if (type == HTTP_CONTENT_SWF && swfFlag == 1 && 
      ((len + buf - end) > SWF_SAVE_FOOTER_LEN + SWF_SAVE_HEADER_LEN + 8))
    return 1;

  return 0;
}



