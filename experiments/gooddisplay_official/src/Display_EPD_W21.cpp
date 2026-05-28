#include "Display_EPD_W21_spi.h"
#include "Display_EPD_W21.h"
#include  "Arduino.h"
void EPD_W21_Init(void)
{
  EPD_W21_RST_0;    // Module reset
  delay(10);//At least 10ms delay 
  EPD_W21_RST_1;
  delay(10);//At least 10ms delay 
  
}

void lcd_chkstatus(void)
{
  while(!isEPD_W21_BUSY);                         
}

void EPD_init(void)
{ 

  EPD_W21_Init(); //Electronic paper IC reset   
  
  EPD_W21_WriteCMD(0xAA);    // CMDH
  EPD_W21_WriteDATA(0x49);
  EPD_W21_WriteDATA(0x55);
  EPD_W21_WriteDATA(0x20);
  EPD_W21_WriteDATA(0x08);
  EPD_W21_WriteDATA(0x09);
  EPD_W21_WriteDATA(0x18);

  EPD_W21_WriteCMD(PWRR);//
  EPD_W21_WriteDATA(0x3F);
  
  EPD_W21_WriteCMD(PSR);  
  EPD_W21_WriteDATA(0x5F);
  EPD_W21_WriteDATA(0x69);
  
  EPD_W21_WriteCMD(POFS);
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteDATA(0x54);
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteDATA(0x44); 

  EPD_W21_WriteCMD(BTST1);
  EPD_W21_WriteDATA(0x40);
  EPD_W21_WriteDATA(0x1F);
  EPD_W21_WriteDATA(0x1F);
  EPD_W21_WriteDATA(0x2C);
  
  EPD_W21_WriteCMD(BTST2);
  EPD_W21_WriteDATA(0x6F);
  EPD_W21_WriteDATA(0x1F);
  EPD_W21_WriteDATA(0x17);
  EPD_W21_WriteDATA(0x49);

  EPD_W21_WriteCMD(BTST3);
  EPD_W21_WriteDATA(0x6F);
  EPD_W21_WriteDATA(0x1F);
  EPD_W21_WriteDATA(0x1F);
  EPD_W21_WriteDATA(0x22);
  EPD_W21_WriteCMD(PLL);
  EPD_W21_WriteDATA(0x08);
  EPD_W21_WriteCMD(CDI);
  EPD_W21_WriteDATA(0x3F);
  
  EPD_W21_WriteCMD(TCON);
  EPD_W21_WriteDATA(0x02);
  EPD_W21_WriteDATA(0x00);
  
  EPD_W21_WriteCMD(TRES);
  EPD_W21_WriteDATA(0x03);
  EPD_W21_WriteDATA(0x20);
  EPD_W21_WriteDATA(0x01); 
  EPD_W21_WriteDATA(0xE0);

  EPD_W21_WriteCMD(T_VDCS);
  EPD_W21_WriteDATA(0x01);
  
  EPD_W21_WriteCMD(PWS);
  EPD_W21_WriteDATA(0x2f);

  EPD_W21_WriteCMD(0x04);     //PWR on  
  lcd_chkstatus();          //waiting for the electronic paper IC to release the idle signal
}
 

void EPD_sleep(void)
{
    EPD_W21_WriteCMD(0X02);   //power off
    EPD_W21_WriteDATA(0x00);
    lcd_chkstatus();          //waiting for the electronic paper IC to release the idle signal
    /*EPD_W21_WriteCMD(0X07);   //deep sleep
    EPD_W21_WriteDATA(0xA5);*/
}

unsigned char Color_get(unsigned char color)
{
  unsigned datas;
  switch(color)
  {
      case 0x00: // Black
            datas = 0x00; 
            break;
        case 0xff: // White
            datas = 0x01; 
            break;
        case 0xfc: // Yellow
            datas = 0x02; 
            break;
        case 0xE0: // Red
            datas = 0x03; 
            break;
        case 0x03: // Blue
            datas = 0x05; 
            break;
        case 0x1c: // Green
            datas = 0x06; 
            break;
        default:
            datas = 0x00; 
            break;
    }
    return datas;
}

void PIC_display(const unsigned char* picData)
{
  unsigned int i,j,k;
  unsigned char temp1,temp2;
  unsigned char data_H,data_L,data;
  

   //Acep_color(White); //Each refresh must be cleaned first   
  EPD_W21_WriteCMD(0x10);        
  for(i=0;i<480;i++)
  { 
    k=0;
    for(j=0;j<800/2;j++)
    {
      
      temp1=picData[i*800+k++]; 
      temp2=picData[i*800+k++];
      data_H=Color_get(temp1)<<4;
      data_L=Color_get(temp2);
      data=data_H|data_L;
      EPD_W21_WriteDATA(data);
    }
  } 
Serial.println("start");

   //Refresh
    EPD_W21_WriteCMD(0x12);   //DISPLAY REFRESH   
    EPD_W21_WriteDATA(0x00);

    delay(1);   //!!!The delay here is necessary, 200uS at least!!!     
    lcd_chkstatus();          //waiting for the electronic paper IC to release the idle signal
Serial.println("done");
}


void PIC_display_Clear(void)
{
   unsigned int i,j;
  //Acep_color(Clean); //Each refresh must be cleaned first 
  EPD_W21_WriteCMD(0x10);        
  for(i=0;i<480;i++)
  {
    for(j=0;j<800/2;j++)
    {
      EPD_W21_WriteDATA(White);
    }
  }
    //Refresh
  EPD_W21_WriteCMD(0x12);   //DISPLAY REFRESH   
  EPD_W21_WriteDATA(0x00);
  delay(1);              //!!!The delay here is necessary, 200uS at least!!!     
  lcd_chkstatus();          //waiting for the electronic paper IC to release the idle signal

}

void EPD_Display_White(void)
{
  unsigned long i;
 

  
  EPD_W21_WriteCMD(DTM);
  {
    for(i=0;i<192000;i++)
    {
      EPD_W21_WriteDATA(0x11);
    }
  }
  EPD_W21_WriteCMD(PON);
  lcd_chkstatus();
  
  //20211212
  //Second setting
  EPD_W21_WriteCMD(BTST2);
  EPD_W21_WriteDATA(0x6F);
  EPD_W21_WriteDATA(0x1F);
  EPD_W21_WriteDATA(0x17);
  EPD_W21_WriteDATA(0x49);
    
  
  EPD_W21_WriteCMD(DRF);
  EPD_W21_WriteDATA(0x00);
  lcd_chkstatus();
  
 
  EPD_W21_WriteCMD(POF);
  EPD_W21_WriteDATA(0x00);
  lcd_chkstatus();

}


void EPD_Display_Black(void)
{
  unsigned long i;
 

  
  EPD_W21_WriteCMD(DTM);
  {
    for(i=0;i<192000;i++)
    {
      EPD_W21_WriteDATA(0x00);
    }
  }
  EPD_W21_WriteCMD(PON);
  lcd_chkstatus();
  
  //20211212
  //Second setting
  EPD_W21_WriteCMD(BTST2);
  EPD_W21_WriteDATA(0x6F);
  EPD_W21_WriteDATA(0x1F);
  EPD_W21_WriteDATA(0x17);
  EPD_W21_WriteDATA(0x49);
    
  
  EPD_W21_WriteCMD(DRF);
  EPD_W21_WriteDATA(0x00);
  lcd_chkstatus();
  
 
  EPD_W21_WriteCMD(POF);
  EPD_W21_WriteDATA(0x00);
  lcd_chkstatus();

}

void EPD_Display_red(void)
{
  unsigned long i;
 

  
  EPD_W21_WriteCMD(DTM);
  {
    for(i=0;i<192000;i++)
    {
      EPD_W21_WriteDATA(0x33);
    }
  }
  EPD_W21_WriteCMD(PON);
  lcd_chkstatus();
  
  //20211212
  //Second setting
  EPD_W21_WriteCMD(BTST2);
  EPD_W21_WriteDATA(0x6F);
  EPD_W21_WriteDATA(0x1F);
  EPD_W21_WriteDATA(0x17);
  EPD_W21_WriteDATA(0x49);
    
  
  EPD_W21_WriteCMD(DRF);
  EPD_W21_WriteDATA(0x00);
  lcd_chkstatus();
  
 
  EPD_W21_WriteCMD(POF);
  EPD_W21_WriteDATA(0x00);
  lcd_chkstatus();

}
void EPD_Display_Yellow(void)
{
  unsigned long i;
 

  
  EPD_W21_WriteCMD(DTM);
  {
    for(i=0;i<192000;i++)
    {
      EPD_W21_WriteDATA(0x22);
    }
  }
  EPD_W21_WriteCMD(PON);
  lcd_chkstatus();
  
  //20211212
  //Second setting
  EPD_W21_WriteCMD(BTST2);
  EPD_W21_WriteDATA(0x6F);
  EPD_W21_WriteDATA(0x1F);
  EPD_W21_WriteDATA(0x17);
  EPD_W21_WriteDATA(0x49);
    
  
  EPD_W21_WriteCMD(DRF);
  EPD_W21_WriteDATA(0x00);
  lcd_chkstatus();
  
 
  EPD_W21_WriteCMD(POF);
  EPD_W21_WriteDATA(0x00);
  lcd_chkstatus();

}

void EPD_Display_blue(void)
{
  unsigned long i;
 

  
  EPD_W21_WriteCMD(DTM);
  {
    for(i=0;i<192000;i++)
    {
      EPD_W21_WriteDATA(0x55);
    }
  }
  EPD_W21_WriteCMD(PON);
  lcd_chkstatus();
  
  //20211212
  //Second setting
  EPD_W21_WriteCMD(BTST2);
  EPD_W21_WriteDATA(0x6F);
  EPD_W21_WriteDATA(0x1F);
  EPD_W21_WriteDATA(0x17);
  EPD_W21_WriteDATA(0x49);
    
  
  EPD_W21_WriteCMD(DRF);
  EPD_W21_WriteDATA(0x00);
  lcd_chkstatus();
  
 
  EPD_W21_WriteCMD(POF);
  EPD_W21_WriteDATA(0x00);
  lcd_chkstatus();

}

void EPD_Display_Green(void)
{
  unsigned long i;
 

  
  EPD_W21_WriteCMD(DTM);
  {
    for(i=0;i<192000;i++)
    {
      EPD_W21_WriteDATA(0x66);
    }
  }
  EPD_W21_WriteCMD(PON);
  lcd_chkstatus();
  
  //20211212
  //Second setting
  EPD_W21_WriteCMD(BTST2);
  EPD_W21_WriteDATA(0x6F);
  EPD_W21_WriteDATA(0x1F);
  EPD_W21_WriteDATA(0x17);
  EPD_W21_WriteDATA(0x49);
    
  
  EPD_W21_WriteCMD(DRF);
  EPD_W21_WriteDATA(0x00);
  lcd_chkstatus();
  
 
  EPD_W21_WriteCMD(POF);
  EPD_W21_WriteDATA(0x00);
  lcd_chkstatus();

}
/*
//// 实现局部刷新函数
void EPD_PartialWindow(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t color) {
    // 计算结束坐标
    uint16_t x_end = x + width - 1;
    uint16_t y_end = y + height - 1;

    // 1. 发送局部刷新命令 (0x83)
    EPD_W21_WriteCMD(0x83);

    // 2. 设置水平起始地址 (HRST[9:0])
    EPD_W21_WriteDATA((x >> 8) & 0x03);  // A[9:8]
    EPD_W21_WriteDATA(x & 0xFF);         // A[7:0]

    // 3. 设置水平结束地址 (HRED[9:0])
    EPD_W21_WriteDATA((x_end >> 8) & 0x03);  // B[9:8]
    EPD_W21_WriteDATA(x_end & 0xFF);         // B[7:0]

    // 4. 设置垂直起始地址 (VRST[9:0])
    EPD_W21_WriteDATA((y >> 8) & 0x03);  // C[9:8]
    EPD_W21_WriteDATA(y & 0xFF);         // C[7:0]

    // 5. 设置垂直结束地址 (VRED[9:0])
    EPD_W21_WriteDATA((y_end >> 8) & 0x03);  // D[9:8]
    EPD_W21_WriteDATA(y_end & 0xFF);         // D[7:0]

    // 6. 启用局部刷新模式 (E[0] = 1)
    EPD_W21_WriteDATA(0x01);  // E[0] = 1

    // 7. 等待显示屏空闲
    lcd_chkstatus();

    // 8. 填充颜色数据
    // 假设显示屏使用 1 字节表示 1 像素（根据你的颜色定义）
for (uint16_t j = 0; j < height; j++) {
        for (uint16_t i = 0; i < width; i += 2) {  // 每次处理 2 个像素
            uint8_t data;
            if (i + 1 < width) {
                // 两个像素都可以填充颜色
                data = (color << 4) | (color & 0x0F);  // 高 4 位和低 4 位都填充 color
            } else {
                // 最后一个像素（如果宽度是奇数）
                data = (color << 4);  // 只填充高 4 位，低 4 位保持 0
            }
            EPD_W21_WriteDATA(data);  // 发送 1 个字节（2 个像素）
        }
    }

    // 9. 等待显示屏完成刷新
    lcd_chkstatus();
}*/