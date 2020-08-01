#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/wait.h>
#include <linux/platform_device.h>
#include <linux/clk.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/div64.h>

#include <asm/mach/map.h>
#include <linux/fb.h>
#include <asm/types.h>

#define         VIDCON0                 0x00//视频寄存器0
#define         VIDCON1                 0x04//视频寄存器1
#define         VIDCON2                 0x08//视频寄存器2
#define         VIDCON3                 0x0c//视频寄存器3
#define         VIDTCON0                0x10//LCD时序控制寄存器0
#define         VIDTCON1                0x14//LCD时序控制寄存器1
#define         VIDTCON2                0x18//LCD时序控制寄存器2
#define         VIDTCON3                0x1c//LCD时序控制寄存器3
#define         WINCON0                 0x20//窗口控制寄存器0
#define         SHADOWCON               0x34//阴影控制寄存器
#define         WINCHMAP2               0x3c//窗口和通道映射寄存器
#define         VIDOSD0A                0x40//窗口0位置控制寄存器
#define         VIDOSD0B                0x44//窗口0位置控制寄存器
#define         VIDOSD0C                0x48//窗口0大小控制寄存器

#define         VIDW00ADD0B0    0xA0//指定窗口起始地址寄存器
#define         VIDW00ADD1B0    0xD0//指定窗口借宿地址寄存器

#define         CLK_SRC_LCD0            0x234//时钟源配置寄存器偏移
#define         CLK_SRC_MASK_LCD        0x334//时钟复用器输出掩码，掩着就不输出
#define         CLK_DIV_LCD             0x534//FIMD时钟分频
#define         CLK_GATE_IP_LCD         0x934//时钟输出掩码

#define         LCDBLK_CFG                      0x00//系统控制LCD寄存器偏移，配置RGB接口或80接口等
#define         LCDBLK_CFG2                     0x04//PWM配置

#define         LCD_LENTH                       480
#define         LCD_WIDTH                       272
#define         BITS_PER_PIXEL          16



static int s3c_lcdfb_setcolreg(unsigned int regno, unsigned int red,
                               unsigned int green, unsigned int blue,
                               unsigned int transp, struct fb_info *info);


static struct fb_ops s3c_lcdfb_ops =
{
    .owner              = THIS_MODULE,
    .fb_setcolreg       = s3c_lcdfb_setcolreg,
    .fb_fillrect        = cfb_fillrect,
    .fb_copyarea        = cfb_copyarea,
    .fb_imageblit       = cfb_imageblit,
};


static struct fb_info *s3c_lcd;
static volatile void __iomem *lcd_regs_base;
static volatile void __iomem *clk_regs_base;
static volatile void __iomem *lcdblk_regs_base;
static volatile void __iomem *lcd0_configuration;
static u32 pseudo_palette[16];
static struct resource *res1, *res2, *res3, *res4;

/* from pxafb.c */
static inline unsigned int chan_to_field(unsigned int chan, struct fb_bitfield *bf)
{
    chan &= 0xffff;
    chan >>= 16 - bf->length;
    return chan << bf->offset;
}


static int s3c_lcdfb_setcolreg(unsigned int regno, unsigned int red,
                               unsigned int green, unsigned int blue,
                               unsigned int transp, struct fb_info *info)
{
    unsigned int color = 0;
        uint32_t *p;

        color  = chan_to_field(red,     &info->var.red);
        color |= chan_to_field(green, &info->var.green);
        color |= chan_to_field(blue, &info->var.blue);

        p = info->pseudo_palette;  
    p[regno] = color;
    return 0;
}

static int lcd_probe(struct platform_device *pdev)
{
    printk("----------------- LCD DEVICE Probe -----------------\n");
    int ret;
    unsigned int temp;

    /* 1. 分配一个fb_info */
    s3c_lcd = framebuffer_alloc(0, NULL);

    /* 2. 设置 */
    /* 2.1 设置 fix 固定的参数 */
    strcpy(s3c_lcd->fix.id, "lcd4_3");
    s3c_lcd->fix.smem_len = LCD_LENTH * LCD_WIDTH * BITS_PER_PIXEL / 8;     //显存的长度
    s3c_lcd->fix.type     = FB_TYPE_PACKED_PIXELS;                                                      //类型
    s3c_lcd->fix.visual   = FB_VISUAL_TRUECOLOR;                                                        //TFT 真彩色
    s3c_lcd->fix.line_length = LCD_LENTH * BITS_PER_PIXEL / 8;                          //一行的长度
    /* 2.2 设置 var 可变的参数 */
    s3c_lcd->var.xres           = LCD_LENTH;                    //x方向分辨率
    s3c_lcd->var.yres           = LCD_WIDTH;                    //y方向分辨率
    s3c_lcd->var.xres_virtual   = LCD_LENTH;                    //x方向虚拟分辨率
    s3c_lcd->var.yres_virtual   = LCD_WIDTH;                    //y方向虚拟分辨率
    s3c_lcd->var.bits_per_pixel = BITS_PER_PIXEL;               //每个像素占的bit
    /* RGB:565 */

    s3c_lcd->var.red.length     = 5;
    s3c_lcd->var.red.offset     = 11;   //红
    s3c_lcd->var.green.length   = 6;
    s3c_lcd->var.green.offset   = 5;    //绿
    s3c_lcd->var.blue.length    = 5;
    s3c_lcd->var.blue.offset    = 0;    //蓝
    s3c_lcd->var.activate       = FB_ACTIVATE_NOW;
    /* 2.3 设置操作函数 */
    s3c_lcd->fbops              = &s3c_lcdfb_ops;

    /* 2.4 其他的设置 */
    s3c_lcd->pseudo_palette     = pseudo_palette;               //调色板
    s3c_lcd->screen_size        = LCD_LENTH * LCD_WIDTH * BITS_PER_PIXEL / 8;   //显存大小

    /* 3. 硬件相关的操作 */
    /* 3.1 配置GPIO用于LCD */
    //设备树中使用"default"
    /* 3.2 根据LCD手册设置LCD控制器, 比如VCLK的频率等 */
    //寄存器映射
    
    //LCD控制寄存器基地址0x11C00000，映射大小0x20c0，配置LCD参数用的
    res1 = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    if (res1 == NULL)
    {
        printk("platform_get_resource error\n");
        return -EINVAL;
    }
    
    lcd_regs_base = devm_ioremap_resource(&pdev->dev, res1);
    if (lcd_regs_base == NULL)
    {
        printk("devm_ioremap_resource error\n");
        return -EINVAL;
    }

    //SYSREG控制寄存器基地址0x10010210，映射大小0x08，控制显示控制器用的
    res2 = platform_get_resource(pdev, IORESOURCE_MEM, 1);
    if (res2 == NULL)
    {
        printk("platform_get_resource error\n");
        return -EINVAL;
    }
    
    lcdblk_regs_base = devm_ioremap_resource(&pdev->dev, res2);
    if (lcdblk_regs_base == NULL)
    {
        printk("devm_ioremap_resource error\n");
        return -EINVAL;
    }

    //配置LCD的供电模式寄存器，基地址0x10023c80，映射大小0x04，控制显示控制器供电
    res3 = platform_get_resource(pdev, IORESOURCE_MEM, 2);
    if (res3 == NULL)
    {
        printk("platform_get_resource error\n");
        return -EINVAL;
    }

    lcd0_configuration = ioremap(res3->start, 0x04);    
    if (lcd0_configuration == NULL)
    {
        printk("devm_ioremap_resource error\n");
        return -EINVAL;
    }
    //配置供电为使能
    *(unsigned long *)lcd0_configuration = 7;

    //配置FIMD时钟源选择，基地址0x1003c000，映射大小0x1000，控制时钟
    res4 = platform_get_resource(pdev, IORESOURCE_MEM, 3);
    if (res3 == NULL)
    {
        printk("platform_get_resource error\n");
        return -EINVAL;
    }
    clk_regs_base = ioremap(res4->start, 0x1000);
    if (clk_regs_base == NULL)
    {
        printk("devm_ioremap_resource error\n");
        return -EINVAL;
    }

    //使能时钟
    //时钟源选择 0110  SCLKMPLL_USER_T 800M
    temp = readl(clk_regs_base + CLK_SRC_LCD0);
    temp &= ~(0x0f);//清空FIMD0_SEL寄存器
    temp |= 0x06;//选择SCLKMPLL_USER_T作为时钟源
    writel(temp, clk_regs_base + CLK_SRC_LCD0);

    //FIMD0_MASK 配置FIMD0_MASK为UMASK
    temp = readl(clk_regs_base + CLK_SRC_MASK_LCD);
    temp |= 0x01;
    writel(temp, clk_regs_base + CLK_SRC_MASK_LCD);

    //SCLK_FIMD0 = MOUTFIMD0/(FIMD0_RATIO + 1),分频为1 800Mhz
    temp = readl(clk_regs_base + CLK_DIV_LCD);
    temp &= ~(0x0f);
    writel(temp, clk_regs_base + CLK_DIV_LCD);

    //CLK_FIMD0 门限配置为Pass
    temp = readl(clk_regs_base + CLK_GATE_IP_LCD);
    temp |= 0x01;
    writel(temp, clk_regs_base + CLK_GATE_IP_LCD);

    //FIMDBYPASS_LBLK0 FIMD Bypass
    temp = readl(lcdblk_regs_base + LCDBLK_CFG);
    temp &= ~(3<<10);//清空VT_LBLK0，选择为RGB接口模式
    temp |= (1 << 1);//FIMDPass
    writel(temp, lcdblk_regs_base + LCDBLK_CFG);

    //使能PWM输出控制
    temp = readl(lcdblk_regs_base + LCDBLK_CFG2);
    temp |= (1 << 0);
    writel(temp, lcdblk_regs_base + LCDBLK_CFG2);
    mdelay(1000);

    /* 配置LCD参数 */
    //分频      800/(79 +1 ) == 10M
    temp = readl(lcd_regs_base + VIDCON0);
    temp |= (79 << 6);
    // printk("VIDCON1 ---> 0x%x",readl(lcd_regs_base + VIDCON0));
    writel(temp, lcd_regs_base + VIDCON0);
    // printk("VIDCON1 ---> 0x%x",readl(lcd_regs_base + VIDCON0));
    /*
     * VIDTCON1:
     * [5]:IVSYNC  ===> 1 : Inverted(反转)  --> set 1
     * [6]:IHSYNC  ===> 1 : Inverted(反转)  --> set 1  LCD手册与控制器手册不通
     * [7]:IVCLK   ===> 1 : Fetches video data at VCLK rising edge (下降沿触发) --> set 0
     * [10:9]:FIXVCLK  ====> 01 : VCLK running  --> set 0
     */

    temp= (1<<6)| (1<<5);
    writel(temp, lcd_regs_base + VIDCON1);
    // printk("VIDCON1 ---> 0x%x",readl(lcd_regs_base + VIDCON1));
    /*
     * VIDTCON0:
     * [23:16]:  VBPD + 1  <------> tvpw
     * [15:8] :  VFPD + 1  <------> tvfp
     * [7:0]  :  VSPW  + 1 <------> tvb - tvpw
     */
    temp = readl(lcd_regs_base + VIDTCON0);
    temp |= (1 << 16) | (1 << 8) | (9);
    writel(temp, lcd_regs_base + VIDTCON0);
    // printk("VIDTCON0 ---->  0%x",temp);
    /*
     * VIDTCON1:
     * [23:16]:  HBPD + 1  <------> thpw 
     * [15:8] :  HFPD + 1  <------> thfp
     * [7:0]  :  HSPW  + 1 <------> thb - thpw
     */
    temp = readl(lcd_regs_base + VIDTCON1);
    temp |= (1 << 16) | (1 << 8)  | (40);
    writel(temp, lcd_regs_base + VIDTCON1);
    // printk("VIDTCON1 ---->  0%x",temp);
    /*
     * HOZVAL = (Horizontal display size) - 1 and LINEVAL = (Vertical display size) - 1.
     * Horizontal(水平) display size : 272
     * Vertical(垂直) display size : 480
     */
    temp = ((LCD_WIDTH-1) << 11) | (LCD_LENTH -1);
    writel(temp, lcd_regs_base + VIDTCON2);

    //Enables VSYNC Signal Output.
    temp = (1<<31);
    writel(temp, lcd_regs_base + VIDTCON3);

    /*
     * WINCON0:
     * [16]:Specifies Half-Word swap control bit. HAWSWP 1 = Enables swap P1779 低位像素存放在低字节
     * [5:2]: Selects Bits Per Pixel (BPP) mode for Window image : 0101 ===> 16BPP RGB565 
     * [1]:Enables/disables video output   1 = Enables
     */
    temp = readl(lcd_regs_base + WINCON0);
    temp &= ~(0xf << 2);// clear BPPMODE
    temp |= (1 << 16) | (5 << 2) | 1;
    writel(temp, lcd_regs_base + WINCON0);
    
    //Window Size For example, Height ? Width (number of word)
    temp = (LCD_LENTH * LCD_WIDTH);
    writel(temp, lcd_regs_base + VIDOSD0C);

    temp = readl(lcd_regs_base + SHADOWCON);
    writel(temp | 0x01, lcd_regs_base + SHADOWCON);
#if 0
    temp = readl(lcd_regs_base + WINCHMAP2);
    temp &= ~(7 << 16);
    temp |= 1 << 16;
    temp &= ~7;
    temp |= 1;
    writel(temp, lcd_regs_base + WINCHMAP2);
    /*
     * bit0-10 : 指定OSD图像左上像素的垂直屏幕坐标
     * bit11-21: 指定OSD图像左上像素的水平屏幕坐标
     */
#endif
    writel(0, lcd_regs_base + VIDOSD0A);
    /*
     * bit0-10 : 指定OSD图像右下像素的垂直屏幕坐标
     * bit11-21: 指定OSD图像右下像素的水平屏幕坐标
     */
    writel(((LCD_LENTH) << 11) | (LCD_WIDTH), lcd_regs_base + VIDOSD0B);
    
    //Enables video output and logic immediately
    temp = readl(lcd_regs_base + VIDCON0);
    writel(temp | 0x03, lcd_regs_base + VIDCON0);

    /* 3.3 分配显存(framebuffer), 并把地址告诉LCD控制器 */
    // s3c_lcd->screen_base         显存虚拟地址
    // s3c_lcd->fix.smem_len        显存大小，前面计算的
    // s3c_lcd->fix.smem_start      显存物理地址
    s3c_lcd->screen_base = dma_alloc_writecombine(NULL, s3c_lcd->fix.smem_len, (dma_addr_t *)&s3c_lcd->fix.smem_start, GFP_KERNEL);

    //显存起始地址
    writel(s3c_lcd->fix.smem_start, lcd_regs_base + VIDW00ADD0B0);
    //显存结束地址
    writel(s3c_lcd->fix.smem_start + s3c_lcd->fix.smem_len, lcd_regs_base + VIDW00ADD1B0);

    /* 4. 注册 */
    ret = register_framebuffer(s3c_lcd);
    return ret;
}

static int lcd_remove(struct platform_device *pdev)
{
    unregister_framebuffer(s3c_lcd);
    dma_free_writecombine(NULL, s3c_lcd->fix.smem_len, s3c_lcd->screen_base, s3c_lcd->fix.smem_start);
    framebuffer_release(s3c_lcd);
    return 0;
}

static const struct of_device_id lcd_dt_ids[] =
{
    { .compatible = "itop4412, lcd_4_3", },
    {},
};

MODULE_DEVICE_TABLE(of, lcd_dt_ids);

static struct platform_driver lcd_driver =
{
    .driver        = {
        .name      = "lcd_4_3",
        .of_match_table    = of_match_ptr(lcd_dt_ids),
    },
    .probe         = lcd_probe,
    .remove        = lcd_remove,
};

static int lcd_init(void)
{
    int ret;
    ret = platform_driver_register(&lcd_driver);

    if (ret)
    {
        printk(KERN_ERR "lcd: probe fail: %d\n", ret);
    }

    return ret;
}

static void lcd_exit(void)
{
    printk("enter %s\n", __func__);
    platform_driver_unregister(&lcd_driver);
}

module_init(lcd_init);
module_exit(lcd_exit);

MODULE_LICENSE("GPL");