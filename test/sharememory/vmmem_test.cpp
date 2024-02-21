/******************************************************************************
  @file    vmmem_test.cpp
  @brief   test share memory between PVM and GVM with vsock

  ---------------------------------------------------------------------------
  Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
  SPDX-License-Identifier: BSD-3-Clause-Clear
  ---------------------------------------------------------------------------
*******************************************************************************/

//#include <vmmem.h>
#include <cstdint>
#include <vmmem_wrapper.h>
#include <base/logging.h>
//#include <BufferAllocator/BufferAllocator.h>
#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <sys/socket.h>
#include <linux/vm_sockets.h>

using namespace std;

#define allocSize  2*1024*1024
#define VSOCK_PORT 0x1234

char testData[allocSize];
char testDataRevert[allocSize];

typedef struct vminfo
{
        int fd;
        unsigned char *ptr;
        int sockfd;
}VMINFO;

VMINFO pvmInfo; //PVM
VMINFO gvmInfo; // GVM

#define PVM2GVM 1
#define GVM2PVM 2

union
{
        struct sockaddr sa;
        struct sockaddr_vm svm;
}addr = {
        .svm={
                .svm_family = AF_VSOCK,
                .svm_port = VSOCK_PORT,
                .svm_cid = VMADDR_CID_ANY,
        },
};
union
{
        struct sockaddr sa;
        struct sockaddr_vm svm;
}clientaddr;

vector<string> getAllFileName(string dirPath)
{
        vector<string> files;
        DIR *dir;
        struct dirent *ptr;

        if ((dir=opendir(dirPath.c_str())) == NULL)
        {
                perror("Open dir error...");
                exit(1);
        }

        while ((ptr=readdir(dir)) != NULL)
        {
                if (strcmp(ptr->d_name, ".") != 0 && strcmp(ptr->d_name, "..") != 0) {
                files.push_back(ptr->d_name);
                }
        }

        closedir(dir);

        cout << dirPath << ": \n";
        for(int i = 0; i <files.size(); i++) {
                cout << files[i] << endl;
        }

        return files;
}

int allocDmaBuf(const string heap_name, size_t size){
        int heap_fd = open(heap_name.c_str(), O_RDWR | O_CLOEXEC);
        if (heap_fd < 0) {
                LOG(ERROR) << "open dma heap failed, heap: " << heap_name;
                return -1;
        }
        struct dma_heap_allocation_data heap_data = {};
        heap_data.len = size;
        heap_data.fd_flags = O_RDWR;

        if (ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &heap_data) != 0) {
                LOG(ERROR) << "dma heap alloc failed, len: " << heap_data.len;
                close(heap_fd);
                return -1;
        }
        return heap_data.fd;
}

int retriveMemory(int64_t handle, int *fd){
        int ret;

        LOG(INFO) << "test CreateVmMem(): Getting a libvmmem object..." ;
        VmMem* vmMem = CreateVmMem();
        if (!vmMem) {
                LOG(ERROR) << "Failed to get a libvmmem object!";
                return -1;
        }

        VmHandle handleMe;
        VmHandle pvmHandle;
        VmHandle handleArr[2];
        int nr = 2;
        uint32_t permArr[2] = {VMMEM_READ |VMMEM_WRITE,
                                VMMEM_READ |VMMEM_WRITE};

        char* vmMe = "qcom,self";
        char* vmDest = "qcom,hlos";

        LOG(INFO) << "test FindVmByName(): Finding the VM ";
        handleMe = FindVmByName(vmMem, vmMe);
        if (handleMe < 0)  {
                LOG(ERROR) << "Failed to find the VM " << vmMe;
                return -1;
        }
        pvmHandle = FindVmByName(vmMem, vmDest);
        if (pvmHandle < 0)  {
                LOG(ERROR) << "Failed to find the VM " << vmDest;
                return -1;
        }

        handleArr[0] = pvmHandle;
        handleArr[1] = handleMe;

        VmHandle handleOwner = handleMe;
        ret = RetrieveDmabuf(vmMem, pvmHandle, handleArr, permArr, nr, handle);
        if (ret < 0) {
                LOG(ERROR) << "retrieve failed!: " << ret;
                return -1;
        }

        *fd = ret;

        return 0;
}

int getShareMemoryHandle(int fd, int64_t* handleOut) {
        int64_t ret;

        /*test VmMem::CreateVmMem()
        get a libvmmem object
        */
        LOG(INFO) << "test CreateVmMem(): Getting a libvmmem object..." ;
        VmMem* vmMem = CreateVmMem();
        if (!vmMem) {
                LOG(ERROR) << "Failed to get a libvmmem object!";
                return -1;
        }

        VmHandle handleMe;
        VmHandle handleDest;

        VmHandle handleArr[2];

        uint32_t permArr[2] = {VMMEM_READ | VMMEM_WRITE,
                                VMMEM_READ | VMMEM_WRITE};

        /*test FindVmByName()
        find the VM
        */
        char* vmMe = "qcom,self";
        char* vmDest = "qcom,roboticsvm1";
        //char* vmDest = "qcom,oemvm";

        LOG(INFO) << "test FindVmByName(): Finding the VM ";
        handleMe = FindVmByName(vmMem, vmMe);
        if (handleMe < 0)  {
                LOG(ERROR) << "Failed to find the VM " << vmMe;
                return -1;
        }
        handleDest = FindVmByName(vmMem, vmDest);
        if (handleDest < 0)  {
                LOG(ERROR) << "Failed to find the VM " << vmDest;
                return -1;
        }

        handleArr[0] = handleMe;
        handleArr[1] = handleDest;

        int nr = 2;
        /*test ShareDmabufHandle();*/
        LOG(INFO) << "test ShareDmabufHandle(): share memory with the GVM..." ;
        ret = ShareDmabufHandle(vmMem, fd, handleArr, permArr, nr, handleOut);;
        if (ret) {
                LOG(ERROR) << "share failed!: " << ret;
                return -1;
        }

        LOG(INFO) << "out handle = " << *handleOut;

        return 0;

}

void initTestData(){
        for(int i = 0; i < allocSize; i++){
                testData[i] = i;
                testDataRevert[i] = allocSize - i - 1;
        }
}

int64_t receiveHandle(){
        int sockfd;
        socklen_t len;
        int64_t handle = 0;
        char *hello = "Hello from client";
        ssize_t size = 0;

        //create socket
        if ((gvmInfo.sockfd = socket(AF_VSOCK, SOCK_DGRAM, 0)) < 0) {
                LOG(ERROR) << "socket creation failed ";
                return -1;
        } else {
                LOG(INFO) << "socket creation success, fd = " << gvmInfo.sockfd;
        }

        LOG(INFO) << "send hello to server";
        size = sendto(gvmInfo.sockfd, hello, strlen(hello) + 1 , 0, &(addr.sa), sizeof(addr));

        LOG(INFO) << "receive handle from PVM";
        size = recvfrom(gvmInfo.sockfd, &handle, sizeof(handle), 0, &(addr.sa), &len);
        if(size < 0){
                LOG(ERROR) << "receive failed ";
                return -1;
        }
        LOG(INFO) << "receive handle = " << handle;
        return (handle > 0) ? handle : -1;
}

int sendHandle(int64_t handle){
        socklen_t len;
        char buffer[1024];

        //create socket
        if ((pvmInfo.sockfd = socket(AF_VSOCK, SOCK_DGRAM, 0)) < 0) {
                LOG(ERROR) << "socket creation failed ";
                return -1;
        } else {
                LOG(INFO) << "socket creation success, fd = " << pvmInfo.sockfd;
        }

        if (bind(pvmInfo.sockfd, &(addr.sa), sizeof(addr.svm)) < 0){
                LOG(ERROR) << "bind failed ";
                return -1;
        }

        ssize_t size = 0;

        len = sizeof(clientaddr.svm);
        LOG(INFO) << "wait to receive hello from client";
        size = recvfrom(pvmInfo.sockfd, buffer, sizeof(buffer), 0, &(clientaddr.sa), &len);
        LOG(INFO) << "received " << buffer;

        LOG(INFO) << "send handle to SVM";
        size = sendto(pvmInfo.sockfd, &handle, sizeof(handle), 0, &(clientaddr.sa), len);
        if(size < 0){
                LOG(ERROR) << "send failed ";
                return -1;
        }
        return 0;
}

int writeData(VMINFO *vminfo, int direction) {
        ssize_t size = 0;
        if(NULL == vminfo->ptr) {
                return -1;
        }
        //fill share memory buffer
        for(int i = 0; i < allocSize; i++) {
                if(direction == PVM2GVM) {
                        vminfo->ptr[i] = testData[i];
                } else if(direction == GVM2PVM) {
                        vminfo->ptr[i] = testDataRevert[i];
                }
        }
        return 0;
}

void readData(VMINFO *vminfo, char *buffer) {
        memcpy(buffer, vminfo->ptr, allocSize);
}

int notify(VMINFO *vminfo, int data) {
        ssize_t size = 0;
        socklen_t len = sizeof(clientaddr.svm);
        size = sendto(vminfo->sockfd, &data, sizeof(int), 0, &(clientaddr.sa), len);
        if(size < 0){
                LOG(ERROR) << "send failed ";
                return -1;
        }
        return 0;
}

int waitForRead(VMINFO *vminfo, int *buffer) {
        ssize_t size = 0;
        socklen_t len = sizeof(clientaddr.svm);
        size = recvfrom(vminfo->sockfd, buffer, sizeof(int), 0, &(clientaddr.sa), &len);
        if(size < 0){
                LOG(ERROR) << "read failed ";
                return -1;
        }
        return 0;
}

/*
PVM: write  then  read

GVM: read  then  write
*/
int testWR(bool isPVM) {
        int ret = 0;
        int sign = 0;
        char buffer[allocSize];

        if(isPVM) { //PVM
                ret = writeData(&pvmInfo, PVM2GVM);
                ret = notify(&pvmInfo, PVM2GVM);

                ret = waitForRead(&pvmInfo, &sign);
                if(sign == GVM2PVM) {
                        readData(&pvmInfo, buffer);
                        for(int i = 0; i < 100; i++){
                                printf("pvm read buffer[%d]=%d \n",i, buffer[i]);
                        }
                        if(strncmp(buffer, testDataRevert, allocSize)) {
                                return -1;
                        }
                }
        } else { //GVM
                ret = waitForRead(&gvmInfo, &sign);
                if(sign == PVM2GVM) {
                        readData(&gvmInfo, buffer);

                        for(int i = 0; i < 100; i++){
                                printf("gvm read buffer[%d]=%d \n",i, buffer[i]);
                        }

                        if(strncmp(buffer, testData, allocSize)) {
                                return -1;
                        }
                }

                ret = writeData(&gvmInfo, GVM2PVM);
                ret = notify(&gvmInfo, GVM2PVM);
        }

        return ret;
}

int testShareMemory(string heap_name){
        int64_t handleOut;

        LOG(INFO) << "allocating memory from heap: "<< heap_name << ", size="<< allocSize;
        pvmInfo.fd = allocDmaBuf("/dev/dma_heap/" + heap_name, allocSize);
        if (pvmInfo.fd < 0) {
                LOG(ERROR) << "Allocation failed!";
                return -1;
        }

        pvmInfo.ptr = (unsigned char*)mmap(NULL, allocSize, PROT_READ | PROT_WRITE, MAP_SHARED, pvmInfo.fd, 0);
        if(pvmInfo.ptr == NULL) {
                LOG(ERROR) << "mmap failed!";
                return -1;
        }

        getShareMemoryHandle(pvmInfo.fd, &handleOut);
        if(handleOut > 0){
                if(sendHandle(handleOut) < 0)
                        return -1;
        }

        return testWR(true);
}

int testRetriveMemory(){
        char *str;
        int ret = 0;

        int64_t handle = receiveHandle();

        if(handle < 0) {
                LOG(ERROR) << "invalid handle";
                return -1;
        }

        LOG(INFO) <<  "handle = " << handle;
        ret = retriveMemory(handle, &gvmInfo.fd);
        if (ret < 0) {
                return -1;
        }
        LOG(INFO) << "get fd = " << gvmInfo.fd;

        gvmInfo.ptr = (unsigned char*)mmap(NULL, allocSize, PROT_READ | PROT_WRITE, MAP_SHARED, gvmInfo.fd, 0);
        if(gvmInfo.ptr == NULL) {

                LOG(ERROR) << "mmap failed!";
                return -1;
        }

        return testWR(false);
}

void printHelp(){
        printf("usage: \n \
                on PVM: vmmem_test s <dma buffer name> \n \
                on GVM: vmmem_test r \n \
                example: vmmem_test s qcom,system \n \
                         vmmem_test r \n \
        ");
}

int main(int argc, char *argv[])
{
        if (argc < 2)
        {
                LOG(INFO) <<"Error,please input test case[s/r], s-> share buffer, r -> rectrieve buffer";
                printHelp();
                return -1;
        }

        initTestData();

        LOG(INFO) << "START" ;
        char test_case = argv[1][0];
        switch (test_case){
                case 's':
                {
                        //vector<string> vm_list = getAllFileName("/dev/mem_buf_vm");
                        //vector<string> heap_list = getAllFileName("/dev/dma_heap");
                        if (argc < 3) {
                                LOG(INFO) <<"Error,please input dma buffer heap name";
                                printHelp();
                                return -1;
                        }
                        string heap_name( argv[2] );

                        int ret = testShareMemory(heap_name);

                        if(ret != 0){
                                printf("test failed! \n");
                        }else {
                                printf("test success! \n");
                        }
                }
                break;
                case 'r':
                {
                        int ret = testRetriveMemory();

                        if(ret != 0){
                                printf("test failed! \n");
                        }else {
                                printf("test success! \n");
                        }
                }
                break;
        }

        return 0;
}
