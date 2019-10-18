#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

int
main(int argc, char *argv[]) {
    fprintf(stdout,"\n----------boot sector 生成程序----------\n\n");    
    fprintf(stdout,"源文件: %s, 目标磁盘文件: %s.\n,",argv[1],argv[2]);
    fprintf(stdout,"原理: 复制源文件至缓冲区,校验大小,把第 511 和 512 个字节置为 0x55 和 0xAA 写入目标文件.\n");

    
    struct stat st;
    if (argc != 3) {
        fprintf(stderr, "Usage: <input filename> <output filename>\n");
        return -1;
    }
    if (stat(argv[1], &st) != 0) {
        fprintf(stderr, "Error opening file '%s': %s\n", argv[1], strerror(errno));
        return -1;
    }
    //printf("'%s' size: %lld bytes\n", argv[1], (long long)st.st_size);
    if (st.st_size > 510) {
        fprintf(stderr, "%lld >> 510!!\n", (long long)st.st_size);
        return -1;
    }else{
        fprintf(stdout, "文件大小校验: 可执行文件大小 = %lld < 510, 校验通过.\n",(long long)st.st_size);
    }
    char buf[512];
    memset(buf, 0, sizeof(buf));
    FILE *ifp = fopen(argv[1], "rb");
    int size = fread(buf, 1, st.st_size, ifp);
    if (size != st.st_size) {
        fprintf(stderr, "read '%s' error, size is %d.\n", argv[1], size);
        return -1;
    }
    fclose(ifp);
    buf[510] = 0x55;
    buf[511] = 0xAA;
    FILE *ofp = fopen(argv[2], "wb+");
    size = fwrite(buf, 1, 512, ofp);
    if (size != 512) {
        fprintf(stderr, "write '%s' error, size is %d.\n", argv[2], size);
        return -1;
    }
    fclose(ofp);
    printf("\n----------512 字节 boot sector: '%s' 构建成功!----------\n\n", argv[2]);
    return 0;
}

