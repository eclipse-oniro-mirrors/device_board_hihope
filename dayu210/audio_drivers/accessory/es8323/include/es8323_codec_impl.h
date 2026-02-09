/*
 * Copyright (C) 2022 HiHope Open Source Organization .
 *
 * HDF is dual licensed: you can use it either under the terms of
 * the GPL, or the BSD license, at your option.
 * See the LICENSE file in the root of this repository for complete details.
 */

#ifndef ES8323_CODEC_IMPL_H
#define ES8323_CODEC_IMPL_H

#include "audio_codec_if.h"
#include "osal_mem.h"
#include "osal_time.h"
#include "osal_io.h"
#include "securec.h"
#include <linux/types.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

enum Es8323I2sFormatRegVal {
    ES8323_I2S_SAMPLE_FORMAT_REG_VAL_24 = 0x0,    /*  24-bit serial audio data word length(default) */
    ES8323_I2S_SAMPLE_FORMAT_REG_VAL_20 = 0x1,    /*  20-bit serial audio data word length */
    ES8323_I2S_SAMPLE_FORMAT_REG_VAL_16 = 0x2,    /*  18-bit serial audio data word length */
    ES8323_I2S_SAMPLE_FORMAT_REG_VAL_18 = 0x3,    /*  16-bit serial audio data word length */
    ES8323_I2S_SAMPLE_FORMAT_REG_VAL_32 = 0x4,    /*  32-bit serial audio data word length */
};

/**
 The following enum values correspond to the location of the configuration parameters in the HCS file.
 If you modify the configuration parameters, you need to modify this value.
*/
enum Es8323DaiHwParamsIndex {
    ES8323_DHP_RENDER_FREQUENCY_INX = 0,
    ES8323_DHP_RENDER_FORMAT_INX = 1,
    ES8323_DHP_RENDER_CHANNEL_INX = 2,
    ES8323_DHP_CAPTURE_FREQUENCY_INX = 3,
    ES8323_DHP_CAPTURE_FORMAT_INX = 4,
    ES8323_DHP_CAPTURE_CHANNEL_INX = 5,
};

struct Es8323DaiHwParamsTransferData {
    uint8_t inputParamsBeginIndex;
    uint8_t inputParamsEndIndex;
    uint8_t otherParamsBeginIndex;
    uint8_t otherParamsEndIndex;
    uint8_t daiHwParamsRegCfgItemCount;
};

/* Original accessory base declare */
enum Es8323I2sFrequency {
    ES8323_I2S_SAMPLE_FREQUENCY_8000  = 8000,    /* 8kHz sample_rate */
    ES8323_I2S_SAMPLE_FREQUENCY_11025 = 11025,   /* 11.025kHz sample_rate */
    ES8323_I2S_SAMPLE_FREQUENCY_12000 = 12000,   /* 12kHz sample_rate */
    ES8323_I2S_SAMPLE_FREQUENCY_16000 = 16000,   /* 16kHz sample_rate */
    ES8323_I2S_SAMPLE_FREQUENCY_22050 = 22050,   /* 22.050kHz sample_rate */
    ES8323_I2S_SAMPLE_FREQUENCY_24000 = 24000,   /* 24kHz sample_rate */
    ES8323_I2S_SAMPLE_FREQUENCY_32000 = 32000,   /* 32kHz sample_rate */
    ES8323_I2S_SAMPLE_FREQUENCY_44100 = 44100,   /* 44.1kHz sample_rate */
    ES8323_I2S_SAMPLE_FREQUENCY_48000 = 48000,   /* 48kHz sample_rate */
    ES8323_I2S_SAMPLE_FREQUENCY_64000 = 64000,   /* 64kHz sample_rate */
    ES8323_I2S_SAMPLE_FREQUENCY_88200 = 88200,   /* 88.2kHz sample_rate */
    ES8323_I2S_SAMPLE_FREQUENCY_96000 = 96000    /* 96kHz sample_rate */
};

enum Es8323I2sFrequencyRegVal {
    ES8323_I2S_SAMPLE_FREQUENCY_REG_VAL_8000  = 0x0,   /* 8kHz sample_rate */
    ES8323_I2S_SAMPLE_FREQUENCY_REG_VAL_11025 = 0x1,   /* 11.025kHz sample_rate */
    ES8323_I2S_SAMPLE_FREQUENCY_REG_VAL_12000 = 0x2,   /* 12kHz sample_rate */
    ES8323_I2S_SAMPLE_FREQUENCY_REG_VAL_16000 = 0x3,   /* 16kHz sample_rate */
    ES8323_I2S_SAMPLE_FREQUENCY_REG_VAL_22050 = 0x4,   /* 22.050kHz sample_rate */
    ES8323_I2S_SAMPLE_FREQUENCY_REG_VAL_24000 = 0x5,   /* 24kHz sample_rate */
    ES8323_I2S_SAMPLE_FREQUENCY_REG_VAL_32000 = 0x6,   /* 32kHz sample_rate */
    ES8323_I2S_SAMPLE_FREQUENCY_REG_VAL_44100 = 0x7,   /* 44.1kHz sample_rate */
    ES8323_I2S_SAMPLE_FREQUENCY_REG_VAL_48000 = 0x8,   /* 48kHz sample_rate */
    ES8323_I2S_SAMPLE_FREQUENCY_REG_VAL_64000 = 0x9,   /* 64kHz sample_rate */
    ES8323_I2S_SAMPLE_FREQUENCY_REG_VAL_88200 = 0xA,   /* 88.2kHz sample_rate */
    ES8323_I2S_SAMPLE_FREQUENCY_REG_VAL_96000 = 0xB    /* 96kHz sample_rate */
};

struct Es8323TransferData {
    uint32_t codecCfgCtrlCount;
    struct AudioRegCfgGroupNode **codecRegCfgGroupNode;
    struct AudioKcontrol *codecControls;
};

struct Es8323DaiParamsVal {
    uint32_t frequencyVal;
    uint32_t formatVal;
    uint32_t channelVal;
};

int32_t Es8323DeviceRegRead(const struct CodecDevice *codec, uint32_t reg, uint32_t *value);
int32_t Es8323DeviceRegWrite(const struct CodecDevice *codec, uint32_t reg, uint32_t value);
int32_t Es8323GetConfigInfo(const struct HdfDeviceObject *device, struct CodecData *codecData);

/* Original es8323 declare */
int32_t Es8323DeviceInit(struct AudioCard *audioCard, const struct CodecDevice *device);
int32_t Es8323DaiDeviceInit(struct AudioCard *card, const struct DaiDevice *device);
int32_t Es8323DaiStartup(const struct AudioCard *card, const struct DaiDevice *device);
int32_t Es8323DaiHwParams(const struct AudioCard *card, const struct AudioPcmHwParams *param);
int32_t Es8323NormalTrigger(const struct AudioCard *card, int cmd, const struct DaiDevice *device);


#define ES8323_CONTROL1         0x00
#define ES8323_CONTROL2         0x01
#define ES8323_CHIPPOWER        0x02
#define ES8323_ADCPOWER         0x03
#define ES8323_DACPOWER         0x04
#define ES8323_CHIPLOPOW1       0x05
#define ES8323_CHIPLOPOW2       0x06
#define ES8323_ANAVOLMANAG      0x07
#define ES8323_MASTERMODE       0x08
#define ES8323_ADCCONTROL1      0x09
#define ES8323_ADCCONTROL2      0x0a
#define ES8323_ADCCONTROL3      0x0b
#define ES8323_ADCCONTROL4      0x0c
#define ES8323_ADCCONTROL5      0x0d
#define ES8323_ADCCONTROL6      0x0e
#define ES8323_ADCCONTROL7      0x0f
#define ES8323_ADCCONTROL8      0x10
#define ES8323_ADCCONTROL9      0x11
#define ES8323_ADCCONTROL10     0x12
#define ES8323_ADCCONTROL11     0x13
#define ES8323_ADCCONTROL12     0x14
#define ES8323_ADCCONTROL13     0x15
#define ES8323_ADCCONTROL14     0x16

#define ES8323_DACCONTROL1      0x17
#define ES8323_DACCONTROL2      0x18
#define ES8323_DACCONTROL3      0x19
#define ES8323_DACCONTROL4      0x1a
#define ES8323_DACCONTROL5      0x1b
#define ES8323_DACCONTROL6      0x1c
#define ES8323_DACCONTROL7      0x1d
#define ES8323_DACCONTROL8      0x1e
#define ES8323_DACCONTROL9      0x1f
#define ES8323_DACCONTROL10     0x20
#define ES8323_DACCONTROL11     0x21
#define ES8323_DACCONTROL12     0x22
#define ES8323_DACCONTROL13     0x23
#define ES8323_DACCONTROL14     0x24
#define ES8323_DACCONTROL15     0x25
#define ES8323_DACCONTROL16     0x26
#define ES8323_DACCONTROL17     0x27
#define ES8323_DACCONTROL18     0x28
#define ES8323_DACCONTROL19     0x29
#define ES8323_DACCONTROL20     0x2a
#define ES8323_DACCONTROL21     0x2b
#define ES8323_DACCONTROL22     0x2c
#define ES8323_DACCONTROL23     0x2d
#define ES8323_DACCONTROL24     0x2e
#define ES8323_DACCONTROL25     0x2f
#define ES8323_DACCONTROL26     0x30
#define ES8323_DACCONTROL27     0x31
#define ES8323_DACCONTROL28     0x32
#define ES8323_DACCONTROL29     0x33
#define ES8323_DACCONTROL30     0x34

#define ES8323_LADC_VOL         ES8323_ADCCONTROL8
#define ES8323_RADC_VOL         ES8323_ADCCONTROL9

#define ES8323_LDAC_VOL         ES8323_DACCONTROL4
#define ES8323_RDAC_VOL         ES8323_DACCONTROL5

#define ES8323_LOUT1_VOL        ES8323_DACCONTROL24
#define ES8323_ROUT1_VOL        ES8323_DACCONTROL25
#define ES8323_LOUT2_VOL        ES8323_DACCONTROL26
#define ES8323_ROUT2_VOL        ES8323_DACCONTROL27

#define ES8323_ADC_MUTE         ES8323_ADCCONTROL7
#define ES8323_DAC_MUTE         ES8323_DACCONTROL3



#define ES8323_IFACE            ES8323_MASTERMODE

#define ES8323_ADC_IFACE        ES8323_ADCCONTROL4
#define ES8323_ADC_SRATE        ES8323_ADCCONTROL5

#define ES8323_DAC_IFACE        ES8323_DACCONTROL1
#define ES8323_DAC_SRATE        ES8323_DACCONTROL2

#define NR_SUPPORTED_MCLK_LRCK_RATIOS 5
/* codec private data */
struct es8323_priv {
	unsigned int sysclk;
	unsigned int allowed_rates[NR_SUPPORTED_MCLK_LRCK_RATIOS];
	struct clk *mclk;
	struct snd_pcm_hw_constraint_list sysclk_constraints;
	struct snd_soc_component *component;
	struct regmap *regmap;
};

extern struct es8323_priv *GetCodecDevice(void);
#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif

#endif
