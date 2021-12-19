/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2021 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under Ultimate Liberty license
  * SLA0044, the "License"; You may not use this file except in compliance with
  * the License. You may obtain a copy of the License at:
  *                             www.st.com/SLA0044
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"
#include "usb_device.h"
#include "stdlib.h"
#include "stm32f4_discovery_audio.h"
#include "fatfs.h"
#include "usb_host.h"


/* You can change the Wave file name as you need, but do not exceed 11 characters */
#define WAVE_NAME "0:leaves.wav"
#define REC_WAVE_NAME "0:rec.wav"


/* Initial Volume level (from 0 (Mute) to 100 (Max)) */
static uint8_t Volume = 70;
uint32_t AudioFreq=48000;

typedef enum
{
  BUFFER_OFFSET_NONE = 0,
  BUFFER_OFFSET_HALF,
  BUFFER_OFFSET_FULL,
}BUFFER_StateTypeDef;


typedef struct
{
  uint32_t   ChunkID;       /* 0 */
  uint32_t   FileSize;      /* 4 */
  uint32_t   FileFormat;    /* 8 */
  uint32_t   SubChunk1ID;   /* 12 */
  uint32_t   SubChunk1Size; /* 16*/
  uint16_t   AudioFormat;   /* 20 */
  uint16_t   NbrChannels;   /* 22 */
  uint32_t   SampleRate;    /* 24 */

  uint32_t   ByteRate;      /* 28 */
  uint16_t   BlockAlign;    /* 32 */
  uint16_t   BitPerSample;  /* 34 */
  uint32_t   SubChunk2ID;   /* 36 */
  uint32_t   SubChunk2Size; /* 40 */

}WAVE_FormatTypeDef;




FIL FileRead;
DIR Directory;

#define AUDIO_BUFFER_SIZE             4096

/* Position in the audio play buffer */
__IO BUFFER_StateTypeDef buffer_offset = BUFFER_OFFSET_NONE;
/* Ping-Pong buffer used for audio play */
uint8_t Audio_Buffer[AUDIO_BUFFER_SIZE];

FATFS USBDISKFatFs;          /* File system object for USB disk logical drive */
char USBDISKPath[4];         /* USB Host logical drive path */



extern USBH_HandleTypeDef hUsbHostFS;
uint8_t repeat_audio=1;

uint8_t WaveRecStatus=0;

void AudioTask(void const * argument);

/*Task state------------------------------*/
#define INIT_FILE            1U
#define WAIT_FOR_ENUM        0U
#define INIT_AUDIO           2U
#define OPEN_FILE            3U
#define READ_AUDIO_START     4U
#define READ_AUDIO_REMAINING 5U
#define ERROR_TASK           6U
#define AUDIO_END            7U

char wav_file[20] = "0:leaves.wav";


/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void FileTask(void const * argument)
{
uint8_t ret;
UINT bytesread = 0;

static uint32_t WaveDataLength;
char path[] = "0:/";
char* wavefilename = NULL;
WAVE_FormatTypeDef waveformat;
uint32_t AudioRemSize;
static uint8_t State =	WAIT_FOR_ENUM;

	while(1)
	{
     osDelay(10U);

     switch (State)
     {

     case WAIT_FOR_ENUM:
     {
    	 if (hUsbHostFS.gState==HOST_CLASS)
    	 {
    	 	 	  State= INIT_FILE;

    	  }
    	  break;
     }

     case INIT_FILE:
     {
    	    /* Initializes the File System */
    	    if (f_mount(&USBDISKFatFs, (TCHAR const*)USBDISKPath, 0 ) != FR_OK )
    	    {
    	      /* FatFs initialisation fails */
    	      Error_Handler();
    	    }
    	    else
    	    {
    	    	State= OPEN_FILE;

    	    }

    	 break;
     }



     case OPEN_FILE:
     {
    	 /* Get the read out protection status */
    	  if(f_opendir(&Directory, path) == FR_OK)
    	  {
    	    if(WaveRecStatus == 1)
    	    {
    	      wavefilename = REC_WAVE_NAME;
    	    }
    	    else
    	    {
    	      wavefilename = wav_file;
    	    }

    	    if(f_open(&FileRead, wavefilename , FA_READ) != FR_OK)
    	    {
    	    	State= ERROR_TASK;
    	    }
    	    else
    	    {
    	    	/*Read the wave header*/
    	        /* Read sizeof(WaveFormat) from the selected file */
    	        f_read (&FileRead, &waveformat, sizeof(waveformat), &bytesread);

    	        /* Set WaveDataLenght to the Speech Wave length */
    	        WaveDataLength = waveformat.FileSize;

    	        /*Read the file */
    	        State=INIT_AUDIO;
    	        break;

    	    }


    	  }
    	  else
    	  {
    			State= ERROR_TASK;
    	  }


    	 break;
     }

     case INIT_AUDIO:
     {
    	 if (waveformat.NbrChannels==1)
    	 {
    		 waveformat.SampleRate=waveformat.SampleRate/2;
    	 }

    	/* Initialize the Audio codec and all related peripherals (I2S, I2C, IOExpander, IOs...) */
        ret=BSP_AUDIO_OUT_Init(OUTPUT_DEVICE_AUTO, Volume, waveformat.SampleRate);
        if (ret ==0)
        {
        	State= READ_AUDIO_START;
        }
        else
        {
        	State= ERROR_TASK;
        }
    	 break;
     }


     case READ_AUDIO_START:
     {
     	  /* Get Data from USB Flash Disk */
    	 f_lseek(&FileRead, 0);

    	 f_read (&FileRead, &Audio_Buffer[0], AUDIO_BUFFER_SIZE, &bytesread);
    	 AudioRemSize = WaveDataLength - bytesread;

       /*Start play sound  */
   	    BSP_AUDIO_OUT_Play((uint16_t*)&Audio_Buffer[0], AUDIO_BUFFER_SIZE);


   		State= READ_AUDIO_REMAINING;


    	 break;
     }

     case READ_AUDIO_REMAINING:
     {

   	  while(AudioRemSize != 0)
       {

   	      bytesread = 0;

         if(buffer_offset == BUFFER_OFFSET_HALF)
         {
             f_read(&FileRead,  &Audio_Buffer[0],
                    AUDIO_BUFFER_SIZE/2,
                    (void *)&bytesread);
       	  buffer_offset = BUFFER_OFFSET_NONE;
         }
         if(buffer_offset == BUFFER_OFFSET_FULL)
         {
           f_read(&FileRead,
                  &Audio_Buffer[AUDIO_BUFFER_SIZE/2],
                  AUDIO_BUFFER_SIZE/2,
                  (void *)&bytesread);

             buffer_offset = BUFFER_OFFSET_NONE;
         }

         if(AudioRemSize > (AUDIO_BUFFER_SIZE / 2))
         {
           AudioRemSize -= bytesread;
         }
         else
         {
           AudioRemSize = 0;

           if (repeat_audio==1U)
           {
        		State= READ_AUDIO_START;
           }
           else
           {

        	    	  BSP_AUDIO_OUT_Stop(CODEC_PDWN_HW);
        	    	  /* Close file */
        	    	  f_close(&FileRead);
        	   State= AUDIO_END;
           }



         }



      }

    	 break;
     }


     case AUDIO_END:

     {

    	 break;
     }

     case ERROR_TASK:
     {
    	 break;
     }



     default:
     {
    	 break;
     }


     }




	}



}


/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void AudioTask(void const * argument)
{
    float pi =3.14;
    float value;
    uint16_t freq=432;
    uint8_t ret;

    uint16_t Am=1000;
    uint16_t shift=200;
	/*init */



#if 0
    for (uint16_t i=0; i<AUDIO_BUFFER_SIZE;i++)
    {
      	value= Am*sin (2*pi*freq*i)+shift;
    	Audio_Buffer[i]=value;
    }

#endif





	while(1)
	{

		osDelay(10U); /*100hz*/


	}



}
/**
  * @brief  Manages the DMA full Transfer complete event.
  */
 void BSP_AUDIO_OUT_TransferComplete_CallBack(void)
{
	 buffer_offset = BUFFER_OFFSET_FULL;
	 BSP_AUDIO_OUT_ChangeBuffer((uint16_t*)&Audio_Buffer[0], AUDIO_BUFFER_SIZE / 2);
}

/**
  * @brief  Manages the DMA Half Transfer complete event.
  */
 void BSP_AUDIO_OUT_HalfTransfer_CallBack(void)
{
	  buffer_offset = BUFFER_OFFSET_HALF;
}

/**
  * @brief  Manages the DMA FIFO error event.
  */
 void BSP_AUDIO_OUT_Error_CallBack(void)
{

}

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/

