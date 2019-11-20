/* Copyright 2011, 2012 SRI International
 * See LICENSE for other credits and copying information
 */

#include "util.h"
#include "../payload_server.h"
#include "file_steg.h"
#include "htmlSteg.h"
#include "compression.h"
#include "connections.h"

#include <ctype.h>

#include <event2/buffer.h>

#define startScriptTypeJS "<script type=\"text/javascript\">"
#define endScriptTypeJS "</script>"

void buf_dump(unsigned char* buf, int len, FILE *out);

ssize_t HTMLSteg::headless_capacity(char *cover_body, int body_length)
{
  return static_headless_capacity(cover_body,(size_t) body_length);
}


ssize_t HTMLSteg::capacity(const uint8_t *cover_payload, size_t len)
{
  return static_capacity((char *) cover_payload, (int) len);
}

unsigned int HTMLSteg::static_capacity(char *cover_payload, int body_length)
{
  ssize_t body_offset = extract_appropriate_respones_body(cover_payload, body_length);
  if (body_offset == -1) {
    return 0; //useless payload
}
   return static_headless_capacity(cover_payload + body_offset, (size_t) (body_length - body_offset));
}

unsigned int
HTMLSteg::static_headless_capacity (char* buf, size_t len) {

  // log_debug("at html static headless capacity");
  char *bp, *jsStart, *jsEnd;
  int cnt=0;

  // jump to the beginning of the body of the HTTP message
  bp = buf;
  
  while (bp < (buf+len)) {
    jsStart = strstr(bp, "<script type=\"text/javascript\">");
    if (jsStart == NULL) break;
    bp = jsStart+31;
    jsEnd = strstr(bp, "</script>");
    if (jsEnd == NULL) break;
    // count the number of usable hex char between jsStart+31 and jsEnd

    size_t chunk_len = jsEnd-bp;
    cnt += js_code_block_preliminary_capacity(bp, chunk_len);

    bp += 9;
  } // while (bp < (buf+len))

  int actual_capacity = max(0, (cnt -JS_DELIMITER_SIZE)/2);  
  //log_debug("payload has capacity %d", actual_capacity);
  return actual_capacity;

}

// #define JS_DELIMITER "?"
// #define JS_DELIMITER_REPLACEMENT "."

/**
   this function carry the only major part that is different between a
   js file and html file. As such html file will re-implement it accordingly
   As the result encode and decode function for both types remains the same.

   this is the overloaded version for htmlsteg variety where js code
   is scattered int th html file
*/
//TODO: move to memory safe model
int HTMLSteg::encode_http_body(const char *data, char *jTemplate, char *jData,
                               unsigned int dlen, unsigned int jtlen,
                               unsigned int jdlen)
{
  const char *dp;

  char *jtp,*jdp; // current pointers for data, jTemplate, and jData
  unsigned int encCnt = 0;  // num of data encoded in jData
  int n; // tmp for updating encCnt
  char *jsStart, *jsEnd;
  int skip;
  int scriptLen;
  int fin = 0;

  log_debug("at htmlsteg encode-http trying to encode %d", dlen);
  dp = data;
  jtp = (char*)jTemplate;
  jdp = jData;

  size_t original_dlen = dlen;

  while (encCnt < original_dlen) {
    jsStart = strstr(jtp, startScriptTypeJS);
    if (jsStart == NULL) {
      log_warn("lack of usable JS; can't find startScriptType\n");
      return encCnt;
    }

    skip = strlen(startScriptTypeJS)+jsStart-jtp;
#ifdef DEBUG
    log_debug("copying %d (skip) char from jtp to jdp\n", skip);
#endif

    memcpy(jdp, jtp, skip);
    jtp = jtp+skip; jdp = jdp+skip;
    jsEnd = strstr(jtp, endScriptTypeJS);
    if (jsEnd == NULL) {
      log_warn("lack of usable JS; can't find endScriptType\n");
      return encCnt;
        
    }
      
    // the JS for encoding data is between jsStart and jsEnd
    scriptLen = jsEnd - jtp;
    // n = encode2(dp, jtp, jdp, dlen, jtlen, jdlen, &fin);
    n = encode_in_single_js_block((char*)dp, jtp, jdp, dlen, scriptLen, jdlen, &fin);
    // update encCnt, dp, and dlen based on n
    if (n > 0) {
      encCnt = encCnt+n; dp = dp+n; dlen = dlen-n;
#ifdef DEBUG
      log_debug("%d bytes encoded", encCnt);
#endif      

    }
    // update jtp, jdp, jdlen
    skip = jsEnd-jtp;
    jtp = jtp+skip; jdp = jdp+skip; jdlen = jdlen-skip;
    skip = strlen(endScriptTypeJS);
    memcpy(jdp, jtp, skip);
    jtp = jtp+skip; jdp = jdp+skip; jdlen = jdlen-skip;
  }

  // copy the rest of jTemplate to jdp
  skip = jTemplate+jtlen-jtp;

  // handling the boundary case in which JS_DELIMITER hasn't been
  // added by encode()
  if (fin == 0 && dlen == 0) {
    if (skip > 0) {
      *jtp = JS_DELIMITER;
      jtp = jtp+1; jdp = jdp+1;
      skip--;
    }
  }

  memcpy(jdp, jtp, skip);
  log_debug("%d bytes encoded", encCnt);

  return encCnt;
  
}

int
HTMLSteg::decode_http_body(const char *jData, const char *dataBuf, unsigned int jdlen,
                       unsigned int dataBufSize, int *fin )
{
  const char *jsStart, *jsEnd;
  const char *dp, *jdp; // current pointers for data and jData
  int scriptLen;
  int decCnt = 0;
  int n;
  int dlen = jdlen; //gets rud of unused warning, useless tho
  dp = dataBuf; jdp = jData;

  *fin = 0;
  dlen = dataBufSize; 
  while (*fin == 0) {
    jsStart = strstr(jdp, startScriptTypeJS);
    if (jsStart == NULL) {

      log_warn("Can't find startScriptType for decoding data inside script type JS\n");

      return decCnt;
    }

    jdp = jsStart+strlen(startScriptTypeJS);
    jsEnd = strstr(jdp, endScriptTypeJS);
    if (jsEnd == NULL) {
      log_warn("Can't find endScriptType for decoding data inside script type JS\n");
      return decCnt;
    }

    // the JS for decoding data is between jsStart and jsEnd
    scriptLen = jsEnd - jdp;
    n = decode_single_js_block(jdp, dp, scriptLen, dlen, fin);
    if (n > 0) {
      decCnt = decCnt+n; dlen=dlen-n; dp=dp+n;
    }
    jdp = jsEnd+strlen(endScriptTypeJS);
  } // while (*fin==0)

  return decCnt;

}

/**
   constructor to steup the correct hard-coded type
 */
HTMLSteg::HTMLSteg(PayloadServer* payload_provider, double noise2signal)
  :JSSteg(payload_provider, noise2signal, HTTP_CONTENT_HTML)
{
  //correcting the type

}
