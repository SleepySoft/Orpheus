#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef ORPHEUS_TEST_GENERATED
#include "orpheus_graph.h"
#include "orpheus_control.h"
#else
#include "orpheus_runtime/runtime.h"
#endif
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); return 1; } } while (0)
int main(int argc,char** argv) {
#ifdef ORPHEUS_TEST_GENERATED
    CHECK(argc>=6 && orpheus_generated_init(48000,32)==ORPHEUS_OK);
    int first=2; const char* mode=argv[1];
#define SET(n,k,v) orpheus_control_set_value(n,k,v)
#define ID(id,v) orpheus_control_set_value_id(id,v)
#define GET(n,k,v) orpheus_control_get_value(n,k,v)
#define READ(n,k,v,c) orpheus_control_get_bulk(n,k,v,c)
#define WRITE(n,k,v,c) orpheus_control_write_bulk(n,k,v,c)
#define WRITE_ID(id,v,c) orpheus_control_write_bulk_id(id,v,c)
#define PROCESS() (strcmp(mode,"task")==0 ? orpheus_generated_process_task_default(0) : orpheus_generated_process(32))
#else
    CHECK(argc>=8);
    orpheus::Runtime runtime;
    CHECK(runtime.load_plan(orpheus::Plan::load_from_file(argv[1]),argv[2])==ORPHEUS_OK);
    int first=4; const char* mode=argv[3];
#define SET(n,k,v) runtime.set_parameter(n,k,v)
#define ID(id,v) runtime.write_id(id,v)
#define GET(n,k,v) runtime.get_parameter(n,k,v)
#define READ(n,k,v,c) runtime.get_bulk(n,k,v,c)
#define WRITE(n,k,v,c) runtime.write_bulk(n,k,v,c)
#define WRITE_ID(id,v,c) runtime.write_bulk_id(id,v,c)
#define PROCESS() (strcmp(mode,"task")==0 ? runtime.process_task("default",0) : runtime.process_block(32))
#endif
    CHECK((argc-first)%4==0);
    const unsigned blocks[]={1,1,1,2,1,1,1,1,1};
    const int32_t sequences[]={0,0,1,2,2,4,INT32_MIN,INT32_MIN+1,0};
    FILE* stream=fopen("sequence_matrix_states.bin","wb"); CHECK(stream);
    for (unsigned step=0;step<9;++step) {
        for (int i=first;i<argc;i+=4) {
            const char* node=argv[i];
            size_t count=(size_t)strtoul(argv[i+3],NULL,10); CHECK(count>0 && count<=1024);
            if (step) {
                float values[1025],before[1024],actual[1024];
                for (size_t k=0;k<count;++k) values[k]=((int)(k%9)-4)*.03f+step*.002f;
                CHECK(READ(node,"requested_gains",before,count)==ORPHEUS_OK);
                uint32_t id=(uint32_t)strtoul(argv[i+2],NULL,10);
                CHECK((step%2 ? WRITE(node,"requested_gains",values,count) : WRITE_ID(id,values,count))==ORPHEUS_OK);
                CHECK(WRITE_ID(id,values,count+1)!=ORPHEUS_OK);
                CHECK(READ(node,"requested_gains",actual,count)==ORPHEUS_OK && !memcmp(before,actual,count*4));
            }
            OrpheusValue sequence={ORPHEUS_VALUE_INT,{0}}; sequence.value.i32=sequences[step];
            uint32_t id=(uint32_t)strtoul(argv[i+1],NULL,10);
            CHECK((step%2 ? SET(node,"sequence",sequence) : ID(id,sequence))==ORPHEUS_OK);
        }
        for (unsigned block=0;block<blocks[step];++block) {
            CHECK(PROCESS()==ORPHEUS_OK);
            for (int i=first;i<argc;i+=4) {
                size_t count=(size_t)strtoul(argv[i+3],NULL,10);
                const char* keys[]={"requested_gains","active_gains","accepted_targets"};
                for (unsigned k=0;k<3;++k) {
                    float values[1024];
                    CHECK(READ(argv[i],keys[k],values,count)==ORPHEUS_OK);
                    CHECK(fwrite(values,4,count,stream)==count);
                }
                OrpheusValue value={ORPHEUS_VALUE_INT,{0}};
                CHECK(GET(argv[i],"last_sequence",&value)==ORPHEUS_OK && value.value.i32==sequences[step]);
                CHECK(fwrite(&value.value.i32,4,1,stream)==1);
            }
        }
    }
    CHECK(fclose(stream)==0);
#ifdef ORPHEUS_TEST_GENERATED
    orpheus_generated_teardown();
#endif
    return 0;
}
