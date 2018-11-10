// yfs client.  implements FS operations using extent and lock server
#include "yfs_client.h"
#include "extent_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <random>
#include <string>
#include <sstream>
//#include <fuse_lowlevel.h>
#include "lock_protocol.h"
#include "lock_client.h"

using namespace std;
yfs_client::yfs_client(std::string extent_dst, std::string lock_dst)
{
  ec = new extent_client(extent_dst);
  assert(pthread_mutex_init(&m_, 0) == 0);

}

yfs_client::inum
yfs_client::n2i(std::string n)
{
  std::istringstream ist(n);
  unsigned long long finum;
  ist >> finum;
  return finum;
}

std::string
yfs_client::filename(inum inum)
{
  std::ostringstream ost;
  ost << inum;
  return ost.str();
}

bool
yfs_client::isfile(inum inum)
{
  if(inum & 0x80000000)
    return true;
  return false;
}

bool
yfs_client::isdir(inum inum)
{
  return ! isfile(inum);
}

int
yfs_client::getfile(inum inum, fileinfo &fin)
{
  int r = OK;


  printf("getfile %016llx\n", inum);
  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

  fin.atime = a.atime;
  fin.mtime = a.mtime;
  fin.ctime = a.ctime;
  fin.size = a.size;
  printf("getfile %016llx -> sz %llu\n", inum, fin.size);

 release:

  return r;
}

int
yfs_client::getdir(inum inum, dirinfo &din)
{
  int r = OK;


  printf("getdir %016llx\n", inum);
  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }
  din.atime = a.atime;
  din.mtime = a.mtime;
  din.ctime = a.ctime;

 release:
  return r;
}

/*
 *fuse lookup invokes this lookup 
 *look for parent inum in the map and returns its value string
 * search for the file specified by the name and set attributes and return if found
 * return 0 otherwise
 *
 *return value: 1 if record found 0 otherwise
 *@param: forwarding all the parameter values from fuse lookup
 */

bool 
yfs_client::lookup(unsigned long long parent, const char *name, unsigned long long *inum_, int *size_)
{
  string value;
  int loc;
  unsigned long long inum = 0;
  ostringstream out;
  extent_protocol::attr a;

  printf("\nclient lookup enter ");
  std::cout << name << std::endl;
 
  printf("\nclient lookup lock acquired\n");

  // verify parent is a directory
  if(!isdir(parent)) return false;

  if(ec->get(parent, value) != yfs_client::OK) return false;

  printf("\nclient lookup 1\n");

  if(value.empty()) return false;

  printf("\nclient lookup 2\n");

  loc = value.find(name, 0);
  if(loc != string::npos)
    inum = stoi(strtok(&value[value.find(".", loc+1)+1], ";")) ;

  if(inum == 0) return false;

  printf("\nclient lookup 3\n");
  *inum_ = inum;


  // get attr
  if(ec->getattr(inum, a) != yfs_client::OK) return false;

  *size_ = a.size;

  return 1;
}
/*
 *fuse createhelper invokes this 
 *
 * looks up if file already exists if not generate a random inum and  append the 
 * file inum and name to parent directory structure
 *
 *return status 
 *@param parent parent directory  
 *@param parent parent directory  
 *@param name name of the file  
 *@param fuse_entry_param entire attribute   
 */
yfs_client::status
yfs_client::createhelper(unsigned long long parent, const char *name)
{
  printf("\ncreatehelper ");
  std::cout << "parent " << parent << " name " << name << endl;

  string value;
  string temp;
  unsigned long long inum_;
  int  size; 
  //extend_protocol::attr a;
  ostringstream out;

  //check if file already present
  if(lookup(parent, name, &inum_, &size))
    return yfs_client::OK;
  printf("\nlook returned found in create helper");
  
   ec->get(parent, value);

//genereate a 64 bit random number and check if has 31st bit one (recognising a file)
  do{
    mt19937_64::result_type seed = time(0);
    mt19937_64 mt_rand(seed);
    inum_ =  mt_rand();
  } while(!isfile(inum_));


  //sprintf(temp, "name.%llu;", inum);
  out << "name." << inum_ <<";";
  value  += out.str();
  ec->put(inum_, value);

  return yfs_client::OK;
//include based on test results
#if 0 
  
  getattr(&a);
  
  e->st_mode = S_IFREG | 0666;
  e->st_nlink = 1;
  e->st_atime = a.atime;
  e->st_mtime = a.mtime;
  e->st_ctime = a.ctime;
  e->st_size = a.size ;

#endif

}
/*
 *fuse open invokes this 
 *
 * looks up if file already exists 
 *
 *return true if file exists and opened 
 *@param ino 64bit fuse inum
 *@param fi file information structure
 */
bool
yfs_client::open_file(unsigned long long ino,
      int *inum_, int *direct_io_, int *keep_cache_)
{
  string value;
  unsigned long long inum;
  ostringstream out;
  printf("\n on open file client");

  FILE *fp = fopen("debug.txt" , "w");

  ec->get(ino, value);
  //check if the file already present
  if(value.empty()){
    do{
      mt19937_64::result_type seed = time(0);
      mt19937_64 mt_rand(seed);
      inum =  mt_rand();
      fprintf(fp, "\nloop");
    }
    while(!isfile(inum));

    out << "name." << inum_ <<";";
    value  += out.str();
    ec->put(inum, value);
    return yfs_client::OK;
  }
  *inum_ = ino;
  *direct_io_ = 0;
  *keep_cache_ = 0;

//include based on test results
#if 0 
  
  getattr(&a);
  
  e->st_mode = S_IFREG | 0666;
  e->st_nlink = 1;
  e->st_atime = a.atime;
  e->st_mtime = a.mtime;
  e->st_ctime = a.ctime;
  e->st_size = a.size ;

#endif

  return true;
}
int yfs_client::get_(unsigned long long inum_, string buf){

  return ec->get(inum_, buf);
}
