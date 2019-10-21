#include <defs.h>
#include <stdio.h>
#include <trap.h>
#include <picirq.h>
#include <fs.h>
#include <ide.h>
#include <x86.h>
#include <assert.h>
#include <kdebug.h>

/*
 * 磁盘设备驱动
 * 
 * 
 */ 


/**
 * ISA = Industry Standard Architecture
 * 关于 ISA:
 *  - 维基百科 : https://en.wikipedia.org/wiki/Industry_Standard_Architecture
 *  - hardwarebook: http://www.hardwarebook.info/ISA
 */ 
#define ISA_DATA                0x00
#define ISA_ERROR               0x01
#define ISA_PRECOMP             0x01
#define ISA_CTRL                0x02
#define ISA_SECCNT              0x02
#define ISA_SECTOR              0x03
#define ISA_CYL_LO              0x04
#define ISA_CYL_HI              0x05
#define ISA_SDH                 0x06
#define ISA_COMMAND             0x07
#define ISA_STATUS              0x07

// https://wiki.osdev.org/PCI_IDE_Controller
// http://t13.org/Documents/UploadedDocuments/project/d1410r3b-ATA-ATAPI-6.pdf

#define IDE_BSY                 0x80
#define IDE_DRDY                0x40
#define IDE_DF                  0x20
#define IDE_DRQ                 0x08
#define IDE_ERR                 0x01

#define IDE_CMD_READ            0x20
#define IDE_CMD_WRITE           0x30
#define IDE_CMD_IDENTIFY        0xEC

#define IDE_IDENT_SECTORS       20
#define IDE_IDENT_MODEL         54
#define IDE_IDENT_CAPABILITIES  98
#define IDE_IDENT_CMDSETS       164
#define IDE_IDENT_MAX_LBA       120
#define IDE_IDENT_MAX_LBA_EXT   200

// https://wiki.osdev.org/ATA_PIO_Mode#Primary.2FSecondary_Bus
// http://www.cs.utexas.edu/~dahlin/Classes/UGOS/reading/ide.html
// http://www.lemis.com/grog/echungadmesg.html
// https://www.intel.com/content/www/us/en/support/articles/000005642/technologies.html
// https://www.cse.huji.ac.il/~feit/papers/HighMCCLinux/extractedLnx/linux-2.5.34/drivers/ide/ide-pci.c_ide_setup_pci_device.c.html
// pciide规范 1
// https://www.bswd.com/pciide.pdf

// linux: https://github.com/torvalds/linux/blob/master/drivers/ide/setup-pci.c
// https://wenku.baidu.com/view/571f9c77951ea76e58fafab069dc5022abea4624.html
// http://www.singlix.com/trdos/archive/code_archive/IDE_ATA_ATAPI_Tutorial.pdf

#define IO_BASE0                0x1F0
#define IO_BASE1                0x170
#define IO_CTRL0                0x3F4
#define IO_CTRL1                0x374

#define MAX_IDE                 4
#define MAX_NSECS               128
#define MAX_DISK_NSECS          0x10000000U
#define VALID_IDE(ideno)        (((ideno) >= 0) && ((ideno) < MAX_IDE) && (ide_devices[ideno].valid))

static const struct {
    unsigned short base;        // I/O Base
    unsigned short ctrl;        // Control Base
} channels[2] = {
    {IO_BASE0, IO_CTRL0},
    {IO_BASE1, IO_CTRL1},
};

#define IO_BASE(ideno)          (channels[(ideno) >> 1].base)
#define IO_CTRL(ideno)          (channels[(ideno) >> 1].ctrl)

static struct ide_device {
    unsigned char valid;        // 0 or 1 (If Device Really Exists)
    unsigned int sets;          // Commend Sets Supported
    unsigned int size;          // Size in Sectors
    unsigned char model[41];    // Model in String
} ide_devices[MAX_IDE];

/* 
 * 磁盘是否已经就绪?
 */
static int
ide_wait_ready(unsigned short iobase, bool check_error) {
    int r;
    while ((r = inb(iobase + ISA_STATUS)) & IDE_BSY)
        /* nothing */;
    if (check_error && (r & (IDE_DF | IDE_ERR)) != 0) {
        return -1;
    }
    return 0;
}

/*
 * 驱动层初始化
 *      
 */
void
ide_init(void) {
    LOG_LINE("初始化开始:ide 磁盘控制器");

    static_assert((SECTSIZE % 4) == 0);
    unsigned short ideno, iobase;
    for (ideno = 0; ideno < MAX_IDE; ideno ++) {
        /* assume that no device here */
        ide_devices[ideno].valid = 0;

        iobase = IO_BASE(ideno);

        /* wait device ready */
        ide_wait_ready(iobase, 0);

        /* step1: select drive */
        outb(iobase + ISA_SDH, 0xE0 | ((ideno & 1) << 4));
        ide_wait_ready(iobase, 0);

        /* step2: send ATA identify command */
        outb(iobase + ISA_COMMAND, IDE_CMD_IDENTIFY);
        ide_wait_ready(iobase, 0);

        /* step3: polling */
        if (inb(iobase + ISA_STATUS) == 0 || ide_wait_ready(iobase, 1) != 0) {
            continue ;
        }

        /* device is ok */
        ide_devices[ideno].valid = 1;

        /* read identification space of the device */
        unsigned int buffer[128];
        insl(iobase + ISA_DATA, buffer, sizeof(buffer) / sizeof(unsigned int));

        unsigned char *ident = (unsigned char *)buffer;
        unsigned int sectors;
        unsigned int cmdsets = *(unsigned int *)(ident + IDE_IDENT_CMDSETS);
        /* device use 48-bits or 28-bits addressing */
        if (cmdsets & (1 << 26)) {
            sectors = *(unsigned int *)(ident + IDE_IDENT_MAX_LBA_EXT);
        }
        else {
            sectors = *(unsigned int *)(ident + IDE_IDENT_MAX_LBA);
        }
        ide_devices[ideno].sets = cmdsets;
        ide_devices[ideno].size = sectors;

        /* check if supports LBA */
        assert((*(unsigned short *)(ident + IDE_IDENT_CAPABILITIES) & 0x200) != 0);

        unsigned char *model = ide_devices[ideno].model, *data = ident + IDE_IDENT_MODEL;
        unsigned int i, length = 40;
        for (i = 0; i < length; i += 2) {
            model[i] = data[i + 1], model[i + 1] = data[i];
        }
        do {
            model[i] = '\0';
        } while (i -- > 0 && model[i] == ' ');

        LOG("ide %d: %10u(sectors), '%s'.\n", ideno, ide_devices[ideno].size, ide_devices[ideno].model);
    }

    // enable ide interrupt
    pic_enable(IRQ_IDE1);
    pic_enable(IRQ_IDE2);
    LOG_LINE("初始化开始:ide 磁盘控制器");

}

bool
ide_device_valid(unsigned short ideno) {
    return VALID_IDE(ideno);
}

size_t
ide_device_size(unsigned short ideno) {
    if (ide_device_valid(ideno)) {
        return ide_devices[ideno].size;
    }
    return 0;
}


/**
 * 
 * 功能: 
 *      以扇区为单位读取磁盘内容
 * 参数: 
 *      - 磁盘编号 ideno
 *      - 扇区编号 secno
 *      - 读取目的缓冲 dst
 *      - 要读取的扇区数量 nsesc
 * 
 * 机制:
 *      1. 向指定端口写入指定命令,触发控制器中断
 *      2. 根据要读取的扇区数,把每个指定的扇区读取到
 * 
 */ 
int
ide_read_secs(unsigned short ideno, uint32_t secno, void *dst, size_t nsecs) {
    assert(nsecs <= MAX_NSECS && VALID_IDE(ideno));
    assert(secno < MAX_DISK_NSECS && secno + nsecs <= MAX_DISK_NSECS);
    unsigned short iobase = IO_BASE(ideno), ioctrl = IO_CTRL(ideno);

    ide_wait_ready(iobase, 0);

    // generate interrupt
    outb(ioctrl + ISA_CTRL, 0);
    outb(iobase + ISA_SECCNT, nsecs);
    outb(iobase + ISA_SECTOR, secno & 0xFF);
    outb(iobase + ISA_CYL_LO, (secno >> 8) & 0xFF);
    outb(iobase + ISA_CYL_HI, (secno >> 16) & 0xFF);
    outb(iobase + ISA_SDH, 0xE0 | ((ideno & 1) << 4) | ((secno >> 24) & 0xF));
    outb(iobase + ISA_COMMAND, IDE_CMD_READ);

    int ret = 0;
    // 对于 nsec 个扇区中的每个,
    for (; nsecs > 0; nsecs --, dst += SECTSIZE) {
        if ((ret = ide_wait_ready(iobase, 1)) != 0) {
            goto out;
        }
        // 从 iobase 读取 SECTSIZE 个字节,即一个扇区, 到 *dst 处.
        // SECTSIZE / sizeof(uint32_t) 个 dword  = SECTSIZE 个字节
        insl(iobase, dst, SECTSIZE / sizeof(uint32_t));
    }

out:
    return ret;
}

// 以扇区为单位写入
// 从 src,向ideno号设备的第secno号扇区转移nsecs个扇区的数据.
int
ide_write_secs(unsigned short ideno, uint32_t secno, const void *src, size_t nsecs) {
    assert(nsecs <= MAX_NSECS && VALID_IDE(ideno));
    assert(secno < MAX_DISK_NSECS && secno + nsecs <= MAX_DISK_NSECS);
    unsigned short iobase = IO_BASE(ideno), ioctrl = IO_CTRL(ideno);

    ide_wait_ready(iobase, 0);

    // generate interrupt
    outb(ioctrl + ISA_CTRL, 0);
    outb(iobase + ISA_SECCNT, nsecs);
    outb(iobase + ISA_SECTOR, secno & 0xFF);
    outb(iobase + ISA_CYL_LO, (secno >> 8) & 0xFF);
    outb(iobase + ISA_CYL_HI, (secno >> 16) & 0xFF);
    outb(iobase + ISA_SDH, 0xE0 | ((ideno & 1) << 4) | ((secno >> 24) & 0xF));
    outb(iobase + ISA_COMMAND, IDE_CMD_WRITE);

    int ret = 0;
    for (; nsecs > 0; nsecs --, src += SECTSIZE) {
        if ((ret = ide_wait_ready(iobase, 1)) != 0) {
            goto out;
        }
        // 每次写入512 字节,即 1 个扇区
        outsl(iobase, src, SECTSIZE / sizeof(uint32_t));
    }

out:
    return ret;
}


/**
 * 参考: 深入PCI与PCIe之一：硬件篇 - 老狼的文章 - 知乎
https://zhuanlan.zhihu.com/p/26172972
 * 
 */ 