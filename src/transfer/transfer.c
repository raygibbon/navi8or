#include "nav.h"
#include <stdio.h>
#include <string.h>

int nav_transfer_copy(NavProvider *source,const char *source_path,
                      NavProvider *destination,const char *destination_path,
                      bool overwrite,NavProgressFn progress,void *userdata,
                      char *error,size_t error_size){
    unsigned char buffer[64*1024];
    NavEntry metadata;
    void *reader=NULL,*writer=NULL;
    uint64_t done=0;
    int result=-1;
    if(!strcmp(source_path,destination_path)){snprintf(error,error_size,"source and destination are the same file");return -1;}
    if(source->stat(source,source_path,&metadata,error,error_size))return -1;
    if(metadata.flags&NAV_ENTRY_DIR){snprintf(error,error_size,"directory copy is not implemented");return -1;}
    if(source->open_read(source,source_path,&reader,error,error_size))return -1;
    if(destination->open_write(destination,destination_path,overwrite,&writer,error,error_size))goto finish;
    if(progress)progress(0,metadata.size,userdata);
    for(;;){size_t got=0;if(source->read(source,reader,buffer,sizeof buffer,&got,error,error_size))goto finish;if(got==0)break;if(destination->write(destination,writer,buffer,got,error,error_size))goto finish;done+=(uint64_t)got;if(progress)progress(done,metadata.size,userdata);}
    result=0;
finish:
    if(writer&&destination->close(destination,writer,error,error_size)&&result==0)result=-1;
    if(reader&&source->close(source,reader,error,error_size)&&result==0)result=-1;
    return result;
}
