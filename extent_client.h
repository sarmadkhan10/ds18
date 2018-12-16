// extent client interface.

#ifndef extent_client_h
#define extent_client_h

#include <string>
#include "extent_protocol.h"
#include "rpc.h"

// contains the cached extent data + other info
struct cached_extent {
	bool dirty;
  	bool to_remove;
  	std::string data;
  	extent_protocol::attr attr;

  	cached_extent() {
  		to_remove = false;
  		dirty = false;
  	}
  };

class extent_client {
 private:
  rpcc *cl;
  std::map<extent_protocol::extentid_t, cached_extent> map_client_cache;

 public:
  extent_client(std::string dst);

  extent_protocol::status get(extent_protocol::extentid_t eid, 
			      std::string &buf);
  extent_protocol::status getattr(extent_protocol::extentid_t eid, 
				  extent_protocol::attr &a);
  extent_protocol::status put(extent_protocol::extentid_t eid, std::string buf);
  extent_protocol::status remove(extent_protocol::extentid_t eid);
};

#endif 

