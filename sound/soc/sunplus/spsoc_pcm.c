/*
 * File:         sound/soc/sunplus/spsoc-pcm.c
 * Author:       <@sunplus.com>
 *
 * Created:      Mar 12 2013
 * Description:  ALSA PCM interface for S+ Chip
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/compat.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/vmalloc.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <linux/fs.h>

#include "spsoc_pcm.h"
#include "spsoc_util.h"
#include "aud_hw.h"
#include "spdif.h"


/*--------------------------------------------------------------------------
*						Feature definition
*--------------------------------------------------------------------------*/

#define USE_KELNEL_MALLOC

/*--------------------------------------------------------------------------
*						Hardware definition	/Data structure
*--------------------------------------------------------------------------*/
static const struct snd_pcm_hardware spsoc_pcm_hardware = {
	.info			        = SNDRV_PCM_INFO_MMAP |
					            SNDRV_PCM_INFO_MMAP_VALID |
					            SNDRV_PCM_INFO_INTERLEAVED |
					            SNDRV_PCM_INFO_PAUSE,
	.formats			    = (SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE|SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S24_3BE), //SNDRV_PCM_FMTBIT_S24_3BE,
	.period_bytes_min	= PERIOD_BYTES_MIN_CONS,
	.period_bytes_max	= PERIOD_BYTES_MAX_CONS,
	.periods_min		  = 2,
	.periods_max		  = 256,
	.buffer_bytes_max	= DRAM_PCM_BUF_LENGTH,
	.channels_min		  = 2,
	.channels_max	    = 12,
};

const u8  VolTab_Scale16[16] = {
	0x00,	0x02,	0x03,	0x04,
	0x05,	0x07,	0x09,	0x0c,
	0x11,	0x16,	0x1e,	0x28,
	0x36,	0x47,	0x5f,	0x80,
};

/*--------------------------------------------------------------------------
*							Global Parameters
*--------------------------------------------------------------------------*/
auddrv_param aud_param;

static struct cdev spaud_fops_cdev;	// for file operation
#define spaud_fops_MAJOR	222		// for file operation

/*--------------------------------------------------------------------------
*
*--------------------------------------------------------------------------*/

static void hrtimer_pcm_tasklet(unsigned long priv)
{
	  volatile RegisterFile_Audio * regs0 = (volatile RegisterFile_Audio*)audio_base;//(volatile RegisterFile_Audio *)REG(60,0);
	  struct spsoc_runtime_data *iprtd = (struct spsoc_runtime_data *)priv;
	  struct snd_pcm_substream *substream = iprtd->substream;
	  struct snd_pcm_runtime *runtime = substream->runtime;
	  unsigned long delta;
	  unsigned int cntforend=0,audcntreg=0,appl_ofs;
	  
	  appl_ofs=runtime->control->appl_ptr % runtime->buffer_size;

	  if (atomic_read(&iprtd->running)) {
		    if(iprtd->usemmap_flag==1) {
			      if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
				        if(substream->pcm->device==0)
					          audcntreg=regs0->aud_a0_cnt;

				        if((iprtd->size-audcntreg)>=iprtd->period) {
					          while(regs0->aud_inc_0 != 0)
					          {
						            cntforend++;
						            if(cntforend>=50)
							          break;
					          };
					          regs0->aud_delta_0 = iprtd->period;
					          if(substream->pcm->device==0)
						            regs0->aud_inc_0 = 0x0f;
				        }
				        else
				        {
					          AUD_DEBUG("write fail aud_delta_0\n");
				        }
			      }
			      if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
			      {
			      }
		    }

		    if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
				    iprtd->offset = regs0->aud_a0_ptr&0xfffffc;

            AUD_DEBUG("P:a0_ptr=0x%x cnt 0x%x\n",iprtd->offset, regs0->aud_a0_cnt);
            #if 0 //cchu
            if (iprtd->offset >= iprtd->last_offset)
			          delta = iprtd->offset - iprtd->last_offset;
		        else
			          delta = iprtd->size + iprtd->offset- iprtd->last_offset;
			      #endif
            if (iprtd->offset < iprtd->cnt_for_test)
            {        	  
            	  if ((iprtd->offset%iprtd->period) == 0){
            	  	  #if 0
            	  	  regs0->aud_delta_0 = iprtd->period;
            	  	  if(substream->pcm->device == 0){
            	  	  	  while((regs0->aud_inc_0 & 0xf) != 0);
            	  	  	  regs0->aud_inc_0 = 0xf;
            	  	  }
            	  	  if(substream->pcm->device == 1){
            	  	      while((regs0->aud_inc_0 & TDM_P_INC0) != 0);
            	  	      regs0->aud_inc_0 = TDM_P_INC0;
            	  	  }            	  	                  	                  	      
						        #endif
                    //AUD_DEBUG("*** size 0x%x periods 0x%x aud_a0_ptr 0x%x\n", iprtd->size, iprtd->period, regs0->aud_a0_ptr);
                }else{
                	  appl_ofs = (iprtd->offset + (iprtd->period>>1))/iprtd->period;
                    if (appl_ofs < iprtd->periods){             	  
            	          iprtd->offset = iprtd->period * appl_ofs;
            	      }else
            	      	  iprtd->offset = 0;
            	  }
            }
		        /* If we've transferred at least a period then report it and
		        * reset our poll time */

		        //if (delta >= iprtd->period )  //ending normal
		        {
			          //AUD_INFO("a0_ptr=0x%08x\n",iprtd->offset);
			          iprtd->last_offset = iprtd->offset;
			          snd_pcm_period_elapsed(substream);
		        }
		    }
		    else
		    {
#if 1
			      iprtd->offset = regs0->aud_a22_ptr&0xfffffc;
#endif
			      AUD_DEBUG("C:a22_ptr=0x%x cnt 0x%x\n",iprtd->offset, regs0->aud_a22_cnt);
            
            if (iprtd->offset >= iprtd->last_offset)
			          delta = iprtd->offset - iprtd->last_offset;			          			      
		        else
			          delta = iprtd->size + iprtd->offset- iprtd->last_offset;			          
			     
            if (delta >= iprtd->period )  //ending normal
		        {		        
			          iprtd->last_offset = iprtd->offset;			         
			          snd_pcm_period_elapsed(substream);
			          AUD_DEBUG("C:a1_ptr=0x%x \n",iprtd->offset);
		        }
		    }
		    /* How much data have we transferred since the last period report? */
	  }
}

static enum hrtimer_restart snd_hrtimer_callback(struct hrtimer *hrt)
{
	  struct spsoc_runtime_data *iprtd =container_of(hrt, struct spsoc_runtime_data, hrt);

	  //AUD_INFO("%s %d\n", __func__, atomic_read(&iprtd->running));
	  //if (!atomic_read(&iprtd->running))
    if (atomic_read(&iprtd->running) == 2)
	  {
		    AUD_DEBUG("cancel htrimer");
        atomic_set(&iprtd->running, 0);
		    return HRTIMER_NORESTART;
	  }

	  tasklet_schedule(&iprtd->tasklet);
	  //hrtimer_pcm_tasklet((unsigned long)iprtd);
	  hrtimer_forward_now(hrt, ns_to_ktime(iprtd->poll_time_ns));
	  return HRTIMER_RESTART;
}


/*--------------------------------------------------------------------------*/
/*							ASoC platform driver								*/
/*--------------------------------------------------------------------------*/
static int spsoc_pcm_preallocate_dma_buffer(struct snd_pcm *pcm, int stream)
{
    struct snd_pcm_substream *substream = pcm->streams[stream].substream;
	  struct snd_dma_buffer *buf = &substream->dma_buffer;
	  size_t size = DRAM_PCM_BUF_LENGTH;

	  buf->dev.type = SNDRV_DMA_TYPE_DEV;
	  buf->dev.dev = pcm->card->dev;
	  buf->private_data = NULL;
	  if(stream == SNDRV_PCM_STREAM_PLAYBACK){
		    //if(pcm->device==0){
			      buf->area=(unsigned char *)aud_param.fifoInfo.pcmtx_virtAddrBase;
			      buf->addr=aud_param.fifoInfo.pcmtx_physAddrBase;
		    //}
	  }

	  if(stream == SNDRV_PCM_STREAM_CAPTURE){
		    buf->area=(unsigned char *)aud_param.fifoInfo.mic_virtAddrBase;
		    buf->addr=aud_param.fifoInfo.mic_physAddrBase;
	  }

	  buf->bytes = DRAM_PCM_BUF_LENGTH;
	  if (!buf->area){
		    pr_err("Failed to allocate dma memory\n");
		    pr_err("Please increase uncached DMA memory region\n");
		    return -ENOMEM;
	  }

	  AUD_INFO("spsoc-pcm:"
		"preallocate_dma_buffer: area=%p, addr=%p, size=%d\n",
		(void *) buf->area,
		(void *) buf->addr,
		size);

	  return 0;
}

static int spsoc_pcm_open(struct snd_pcm_substream *substream)
{
	  struct snd_pcm_runtime *runtime = substream->runtime;
	  struct spsoc_runtime_data *prtd;
	  int ret = 0;

	  AUD_INFO("%s IN, stream device num: %d, stream=%d\n", __func__, substream->pcm->device, substream->stream);

	  snd_soc_set_runtime_hwparams(substream, &spsoc_pcm_hardware);

	  /* Ensure that buffer size is a multiple of period size */
	  ret = snd_pcm_hw_constraint_integer(runtime,
					    SNDRV_PCM_HW_PARAM_PERIODS);
	  if (ret < 0)
		    goto out;

	  snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_BYTES, 128);

	  prtd = kzalloc(sizeof(*prtd), GFP_KERNEL);
	  if (prtd == NULL) {
		    AUD_ERR(" memory get error(spsoc_runtime_data)\n");
		    ret = -ENOMEM;
		    goto out;
	  }
	  spin_lock_init(&prtd->lock);
	  runtime->private_data = prtd;

	  prtd->substream = substream;
 	  hrtimer_init(&prtd->hrt, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
 	  prtd->hrt.function = snd_hrtimer_callback;
	  tasklet_init(&prtd->tasklet, hrtimer_pcm_tasklet,(unsigned long)prtd);
	  AUD_INFO("%s OUT \n",__func__ );
    out:
	      return ret;
}

static int spsoc_pcm_close(struct snd_pcm_substream *substream)
{
	struct spsoc_runtime_data *prtd = substream->runtime->private_data;

	AUD_INFO("%s IN, stream direction: %d\n", __func__, substream->stream);
	hrtimer_cancel(&prtd->hrt);
	kfree(prtd);
	return 0;
}

long spsoc_pcm_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return 0;
}

static int spsoc_pcm_hw_params(struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params)
{
	  struct snd_pcm_runtime *runtime = substream->runtime;
	  struct spsoc_runtime_data *prtd = runtime->private_data;
	  volatile RegisterFile_Audio * regs0 = (volatile RegisterFile_Audio*)audio_base;//(volatile RegisterFile_Audio *)REG(60,0);

	  AUD_INFO("%s IN, params_rate=%d, stream=%d\n", __func__,params_rate(params),substream->stream);
	  AUD_INFO("%s, area=0x%x, addr=0x%08x, bytes=0x%x\n", __func__, substream->dma_buffer.area, substream->dma_buffer.addr, substream->dma_buffer.bytes);
	  snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);

	  prtd->usemmap_flag=0;
	  prtd->last_remainder=0;
	  prtd->cnt_for_test=0;
	  prtd->last_appl_ofs=0;
    
	  prtd->dma_buffer = runtime->dma_addr;
	  prtd->dma_buffer_end = runtime->dma_addr + runtime->dma_bytes;
	  prtd->period_size = params_period_bytes(params);
    
	  prtd->size = params_buffer_bytes(params);
	  prtd->periods = params_periods(params);
	  prtd->period = params_period_bytes(params) ;
	  prtd->offset = 0;
	  prtd->last_offset = 0;
	  prtd->trigger_flag=0;
	  prtd->start_threshold=0;
	  atomic_set(&prtd->running, 0);

	  prtd->poll_time_ns =div_u64((u64)params_period_size(params) * 1000000000UL +  params_rate(params) - 1, params_rate(params));
    //prtd->poll_time_ns =div_u64((u64)params_period_size(params) * 1000000000UL +  96000 - 1, 480000);
	  AUD_INFO("prtd->size=0x%lx, prtd->periods=%d, prtd->period=%d\n, period_size=%d poll_time_ns 0x%lx\n",prtd->size,prtd->periods,\
		         prtd->period, params_period_size(params), prtd->poll_time_ns);

    regs0->aud_audhwya = aud_param.fifoInfo.pcmtx_physAddrBase;
 
	  if( substream->stream == SNDRV_PCM_STREAM_PLAYBACK )
    {
    	  switch (substream->pcm->device){
		        case 0:
			          regs0->aud_a0_length = prtd->size;
			          regs0->aud_a1_length = prtd->size;
			          regs0->aud_a2_length = prtd->size;
			          regs0->aud_a3_length = prtd->size;
			          break;
			      case 1:
			      	  prtd->cnt_for_test = prtd->size;
			      	  AUD_INFO("aud_enable 0x%x cnt_for_test 0x%x\n", regs0->aud_enable,  prtd->cnt_for_test);
			      	  
			      	  //prtd->poll_time_ns =div_u64((u64)(params_period_size(params)/4) * 1000000000UL +  params_rate(params) - 1, params_rate(params));
                regs0->aud_a0_length = prtd->cnt_for_test;//DRAM_PCM_BUF_LENGTH;//prtd->size<<2;                		            
                regs0->aud_a1_length = prtd->cnt_for_test;//DRAM_PCM_BUF_LENGTH;// prtd->size<<2;                
                regs0->aud_a2_length = prtd->cnt_for_test;//DRAM_PCM_BUF_LENGTH;// prtd->size<<2;                
		            regs0->aud_a3_length = prtd->cnt_for_test;//DRAM_PCM_BUF_LENGTH;// prtd->size<<2;                	
		            regs0->aud_a4_length = prtd->cnt_for_test;//DRAM_PCM_BUF_LENGTH;// prtd->size<<2;		            
		            regs0->aud_a20_length = prtd->cnt_for_test;//DRAM_PCM_BUF_LENGTH;// prtd->size<<2;
                AUD_INFO("aud_enable 0x%x\n", regs0->aud_enable);
                break;	    
		    }
    }
	  else
	  {
#if 1
        prtd->cnt_for_test = prtd->size;
        AUD_INFO("aud_enable 0x%x\n", regs0->aud_enable);
		    //regs0->aud_audhwya = aud_param.fifoInfo.mic_physAddrBase;		    
        regs0->aud_a22_length = prtd->cnt_for_test;//prtd->size<<2;
        regs0->aud_a23_length = prtd->cnt_for_test;// prtd->size<<2;        
        regs0->aud_a24_length = prtd->cnt_for_test;// prtd->size<<2;        
		    regs0->aud_a25_length = prtd->cnt_for_test;// prtd->size<<2;        
        AUD_INFO("aud_enable 0x%x\n", regs0->aud_enable);
        //regs0->aud_delta_0 = 0x1000;
#endif
	  }
	return 0;
}

static int spsoc_pcm_hw_free(struct snd_pcm_substream *substream)
{
	  struct snd_pcm_runtime *runtime = substream->runtime;
	  struct spsoc_runtime_data *iprtd = runtime->private_data;
	  
	  snd_pcm_set_runtime_buffer(substream, NULL);
	  tasklet_kill(&iprtd->tasklet);
	  AUD_INFO("%s IN, stream direction: %d,device=%d\n", __func__, substream->stream,substream->pcm->device);

	  return 0;
}

static int spsoc_pcm_prepare(struct snd_pcm_substream *substream)
{
    struct snd_pcm_runtime *runtime = substream->runtime;
	  struct spsoc_runtime_data *iprtd = runtime->private_data;
	  volatile RegisterFile_Audio * regs0 = (volatile RegisterFile_Audio*)audio_base;//(volatile RegisterFile_Audio *)REG(60,0);

	  AUD_INFO("%s IN, buffer_size=0x%lx, stream=%d\n", __func__, runtime->buffer_size, substream->stream);

	  //tasklet_kill(&iprtd->tasklet);

	  iprtd->offset = 0;
	  iprtd->last_offset = 0;

	  if( substream->stream == SNDRV_PCM_STREAM_PLAYBACK )
	  {
		    if(substream->pcm->device==0)
		    {
			      regs0->aud_a0_cnt = 0;
			      regs0->aud_a0_ptr = 0;
			      regs0->aud_a1_cnt = 0;
			      regs0->aud_a1_ptr = 0;
			      regs0->aud_a2_cnt = 0;
			      regs0->aud_a2_ptr = 0;
			      regs0->aud_a3_cnt = 0;
			      regs0->aud_a3_ptr = 0;
			      regs0->aud_fifo_reset = 0xf;
			      while ((regs0->aud_fifo_reset&0xf)!=0)
				    delay_ms(1);
		    }else{
        #if 1		
		        AUD_INFO("aud_audhwya 0x%x\n", regs0->aud_audhwya);
		        AUD_INFO("aud_a0_base 0x%x\n", regs0->aud_a0_base);
		        AUD_INFO("aud_a0_length 0x%x\n", regs0->aud_a0_length);
            //AUD_INFO("aud_a1_base 0x%x\n", regs0->aud_a1_base);
		        //AUD_INFO("aud_a1_length 0x%x\n", regs0->aud_a1_length);
            //AUD_INFO("aud_a2_base 0x%x\n", regs0->aud_a2_base);
		        //AUD_INFO("aud_a2_length 0x%x\n", regs0->aud_a2_length);
            //AUD_INFO("aud_a3_base 0x%x\n", regs0->aud_a3_base);
		        //AUD_INFO("aud_a3_length 0x%x\n", regs0->aud_a3_length);
		        //AUD_INFO("aud_a4_base 0x%x\n", regs0->aud_a4_base);
		        //AUD_INFO("aud_a4_length 0x%x\n", regs0->aud_a4_length);
		        //AUD_INFO("aud_a20_base 0x%x\n", regs0->aud_a20_base);
		        //AUD_INFO("aud_a20_length 0x%x\n", regs0->aud_a20_length);
        #endif	
		    }
	  }
	  if( substream->stream == SNDRV_PCM_STREAM_CAPTURE )
	  {
#if 1		
		    AUD_INFO("aud_audhwya 0x%x\n", regs0->aud_audhwya);
		    AUD_INFO("aud_a22_base 0x%x\n", regs0->aud_a22_base);
		    AUD_INFO("aud_a22_length 0x%x\n", regs0->aud_a22_length);
        //AUD_INFO("aud_a23_base 0x%x\n", regs0->aud_a23_base);
		    //AUD_INFO("aud_a23_length 0x%x\n", regs0->aud_a23_length);
        //AUD_INFO("aud_a24_base 0x%x\n", regs0->aud_a24_base);
		    //AUD_INFO("aud_a24_length 0x%x\n", regs0->aud_a24_length);
        //AUD_INFO("aud_a25_base 0x%x\n", regs0->aud_a25_base);
		    //AUD_INFO("aud_a25_length 0x%x\n", regs0->aud_a25_length);

#endif
	  }
	  return 0;
}

static int spsoc_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	  struct snd_pcm_runtime *runtime = substream->runtime;
	  struct spsoc_runtime_data *prtd = runtime->private_data;
	  unsigned int startthreshold=0;
	  volatile RegisterFile_Audio * regs0 = (volatile RegisterFile_Audio*)audio_base;//(volatile RegisterFile_Audio *)REG(60,0);

	  AUD_INFO("%s IN, cmd %d pcm->device %d\n", __func__, cmd, substream->pcm->device);
	  switch (cmd) {
	      case SNDRV_PCM_TRIGGER_START:
	      case SNDRV_PCM_TRIGGER_RESUME:
	      case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		        if((frames_to_bytes(runtime,runtime->start_threshold)%prtd->period)==0)
		        {
			          AUD_INFO("0: frame_bits %d start_threshold %ld\n", runtime->frame_bits, runtime->start_threshold);
			          startthreshold=frames_to_bytes(runtime,runtime->start_threshold);
		        }
		        else
		        {
			          AUD_INFO("1: frame_bits %d start_threshold 0x%lx\n", runtime->frame_bits, runtime->start_threshold);
			          startthreshold=(frames_to_bytes(runtime,runtime->start_threshold)/prtd->period+1)*prtd->period;
		        }

		        if( substream->stream == SNDRV_PCM_STREAM_PLAYBACK )
		        {
			          if(substream->pcm->device==0)
			          {
				            regs0->aud_grm_master_gain = 0x80000000;
			          }
			          if(prtd->usemmap_flag==0)
			          {
				            if( prtd->trigger_flag == 0)
				            {
				            	  AUD_INFO("***a0_ptr=0x%x cnt 0x%x startthreshold=0x%x\n",regs0->aud_a0_ptr, regs0->aud_a0_cnt, startthreshold);
					              regs0->aud_delta_0 = startthreshold;
					              if(substream->pcm->device == 0){
					              	  while((regs0->aud_inc_0 & 0xf) != 0);
					              	  regs0->aud_inc_0 = 0x0f;
					              }
					              if(substream->pcm->device == 1){
					              	  while((regs0->aud_inc_0 & TDM_P_INC0) != 0);
					              	  regs0->aud_inc_0 = TDM_P_INC0;
					              }
				            }
			          }
			          else if(prtd->usemmap_flag==1)
			          {
				            AUD_INFO("prtd->start_threshold=0x%lx",prtd->start_threshold);

				            while(regs0->aud_inc_0 != 0){};

				            regs0->aud_delta_0 = startthreshold;

				            if(substream->pcm->device==0)
					              regs0->aud_inc_0 = 0x0f;
					          //if(substream->pcm->device==1)
					 	        //    regs0->aud_inc_0 = TDM_P_INC0;
			          }
			          prtd->trigger_flag=1;
			          prtd->start_threshold=0;
		        }
		        else if( substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		        {
			          AUD_INFO("C:prtd->start_threshold=0x%x, startthreshold=0x%x",prtd->start_threshold, startthreshold);
                regs0->aud_delta_0 = startthreshold;
			          prtd->start_threshold=0;
			          //if(substream->pcm->device==1)
			          //    regs0->aud_embedded_input_ctrl = (0x1<<12);
			          //else
			          	  regs0->aud_embedded_input_ctrl = 0;
			          //AUD_INFO("aud_embedded_input_ctrl = 0x%x\n", regs0->aud_embedded_input_ctrl);
                
		        }
            while (atomic_read(&prtd->running) != 0);
		        atomic_set(&prtd->running, 1);
		        hrtimer_start(&prtd->hrt, ns_to_ktime(prtd->poll_time_ns),HRTIMER_MODE_REL);
		        break;
	      case SNDRV_PCM_TRIGGER_STOP:
	      case SNDRV_PCM_TRIGGER_SUSPEND:
	      case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
            //atomic_set(&prtd->running, 0);
		        atomic_set(&prtd->running, 2);
		        if( substream->stream == SNDRV_PCM_STREAM_PLAYBACK )
		        {
			          if(substream->pcm->device==0)
			          {
				            regs0->aud_grm_master_gain = 0x0;
			 	            regs0->aud_inc_0 = regs0->aud_inc_0&(~0x0f);
			          }
			          if(substream->pcm->device==1)
			          {
			          	  while ((regs0->aud_inc_0 & TDM_P_INC0) != 0);
			 	            //regs0->aud_inc_0 = regs0->aud_inc_0&(~((0x1<<20)| 0x1f));
			          }
		        }
		        else if( substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		        {
                regs0->aud_inc_0 = regs0->aud_inc_0 & (~TDMPDM_C_INC0);
		        }

		        break;
	      default:
		        AUD_INFO("%s out \n",__func__ );
		        return -EINVAL;
	  }
	   
	  //AUD_INFO("%s OUT, cmd=%d, device num=%d, stream=%d\n", __func__, cmd, substream->pcm->device, substream->stream);
	  return 0;
}

static snd_pcm_uframes_t spsoc_pcm_pointer(struct snd_pcm_substream *substream)
{
	  struct snd_pcm_runtime *runtime = substream->runtime;
	  struct spsoc_runtime_data *prtd = runtime->private_data;
	  snd_pcm_uframes_t offset,prtd_offset;

	  //AUD_INFO("%s IN, ptrd->offset=0x%x\n", __func__,  prtd->offset);
	  if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK )
	  {
		    prtd_offset=prtd->offset;
	  }
	  else
	  {
		    prtd_offset=prtd->offset;
	  }
	  offset = bytes_to_frames(runtime, prtd_offset);
	  AUD_DEBUG("offset=0x%x", offset);
	  return offset;
}

static int spsoc_pcm_mmap(struct snd_pcm_substream *substream, struct vm_area_struct *vma)
{
#if 1
	  struct snd_pcm_runtime *runtime = substream->runtime;
	  struct spsoc_runtime_data *prtd = runtime->private_data;

	  AUD_INFO("%s IN\n", __func__ );
	  int ret = 0;
	  prtd->usemmap_flag=1;
	  AUD_INFO("%s IN, stream direction: %d\n", __func__, substream->stream);
#ifdef USE_KELNEL_MALLOC
	  ret = dma_mmap_writecombine(substream->pcm->card->dev, vma,
				                        runtime->dma_area,
				                        runtime->dma_addr,
				                        runtime->dma_bytes);
#else
	  vma->vm_flags |= VM_IO | VM_RESERVED;
	  vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	  ret = remap_pfn_range(vma, vma->vm_start,
		                      substream->dma_buffer.addr >> PAGE_SHIFT,
		                      vma->vm_end - vma->vm_start, vma->vm_page_prot);
#endif

	  return ret;
#endif
}
unsigned int last_remainder=0;

static int spsoc_pcm_copy(struct snd_pcm_substream *substream, int channel,
		                      snd_pcm_uframes_t pos,
		                      void __user *buf, snd_pcm_uframes_t count)
{
	  volatile RegisterFile_Audio * regs0 = (volatile RegisterFile_Audio*)audio_base;//(volatile RegisterFile_Audio *)REG(60,0);
	  int ret = 0;
	  struct snd_pcm_runtime *runtime = substream->runtime;
	  struct spsoc_runtime_data *prtd = runtime->private_data;
	  char *hwbuf = runtime->dma_area + frames_to_bytes(runtime, pos);
	  unsigned int count_bytes= frames_to_bytes(runtime, count);
  
	  if( substream->stream == SNDRV_PCM_STREAM_PLAYBACK ){
	  	  AUD_DEBUG("###%s IN, buf=0x%x, dma_area=0x%x, pos=0x%lx count_bytes 0x%x\n", __func__, buf, hwbuf, pos, count_bytes);  	  		    
		    //copy_from_user(hwbuf, buf, count_bytes);		    
		    if(prtd->trigger_flag) {
		    	  regs0->aud_delta_0 = prtd->period;
		    	  if(substream->pcm->device==0){
		    	  	  while((regs0->aud_inc_0 & 0xf) != 0);
		    	  	  regs0->aud_inc_0 = 0x0f;
		    	  }
		    	  if(substream->pcm->device==1){		    	  
			          while((regs0->aud_inc_0 & TDM_P_INC0) != 0);
			          regs0->aud_inc_0 = TDM_P_INC0;
			      }			          		      			                            			      
            AUD_DEBUG("*** aud_a0_ptr 0x%x, count_bytes 0x%x\n", regs0->aud_a0_ptr, count_bytes);
			      prtd->last_remainder=(count_bytes+prtd->last_remainder)%4;
			      //copy_from_user(hwbuf, buf, count_bytes); 
		    }
		    else
		    {
		    	  //copy_from_user(hwbuf, buf, count_bytes);
			      prtd->start_threshold+=frames_to_bytes(runtime, count);
		    }
		    copy_from_user(hwbuf, buf, count_bytes); 
	  }

	  if( substream->stream == SNDRV_PCM_STREAM_CAPTURE ){ 
	  	  AUD_DEBUG("###%s IN, buf=0x%x, dma_area=0x%x, pos=0x%lx count_bytes 0x%x\n", __func__, buf, hwbuf, pos, count_bytes);
        while((regs0->aud_inc_0 & TDMPDM_C_INC0) != 0);
        
        copy_to_user(buf, hwbuf , count_bytes);
        
        regs0->aud_delta_0 = count_bytes;//prtd->offset;
        regs0->aud_inc_0 = TDMPDM_C_INC0;                         
	  }
	  return ret;
}


static int pcm_silence(struct snd_pcm_substream *substream,
				  int channel, snd_pcm_uframes_t pos,
				  snd_pcm_uframes_t count)
{
	AUD_INFO("%s IN\n",__func__);

	return 0;
}

static struct snd_pcm_ops spsoc_pcm_ops = {
	.open		= spsoc_pcm_open,
	.close		= spsoc_pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= spsoc_pcm_hw_params,
	.hw_free		= spsoc_pcm_hw_free,
	.prepare		= spsoc_pcm_prepare,
	.trigger		= spsoc_pcm_trigger,
	.pointer		= spsoc_pcm_pointer,
	.mmap		= spsoc_pcm_mmap,
	.copy		= spsoc_pcm_copy,
	.silence	=pcm_silence ,
};

static u64 spsoc_pcm_dmamask = DMA_BIT_MASK(32);


static int spsoc_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	  struct snd_card *card = rtd->card->snd_card;
	  struct snd_pcm *pcm = rtd->pcm;
	  int ret = 0;

	  AUD_INFO("%s IN\n", __func__);

	  if (!card->dev->dma_mask)
		    card->dev->dma_mask = &spsoc_pcm_dmamask;
	  if (!card->dev->coherent_dma_mask)
		    card->dev->coherent_dma_mask = DMA_BIT_MASK(32);

	  if (pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream){
		    ret = spsoc_pcm_preallocate_dma_buffer(pcm,SNDRV_PCM_STREAM_PLAYBACK);
		    if (ret)
			      goto out;
	  }

	  if (pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream){
		    ret = spsoc_pcm_preallocate_dma_buffer(pcm,SNDRV_PCM_STREAM_CAPTURE);
		    if (ret)
			      goto out;
	  }
     
    aud_param.fifoInfo.Buf_TotalLen = aud_param.fifoInfo.TxBuf_TotalLen + aud_param.fifoInfo.RxBuf_TotalLen;
    return 0;

    out:
	      return 0;
}

static void spsoc_pcm_free_dma_buffers(struct snd_pcm *pcm)
{
	  struct snd_pcm_substream *substream;
	  struct snd_dma_buffer *buf;
	  int stream;

	  for (stream = 0; stream < 2; stream++) {
		    substream = pcm->streams[stream].substream;
		    if (!substream)
			      continue;

		    buf = &substream->dma_buffer;
		    if (!buf->area)
			      continue;
		    buf->area = NULL;
	  }
	  //AUD_INFO("%s IN\n", __func__);
}

int spsoc_reg_mmap(struct file *fp, struct vm_area_struct *vm)
{
	  unsigned long pfn;
	
	  AUD_INFO("%s IN\n", __func__);
	  vm->vm_flags |= VM_IO ;//| VM_RESERVED;
	  vm->vm_page_prot = pgprot_noncached(vm->vm_page_prot);
	  pfn = REG_BASEADDR >> PAGE_SHIFT;

	  return remap_pfn_range(vm, vm->vm_start,
		                       pfn, vm->vm_end-vm->vm_start,
		                       vm->vm_page_prot) ? -EAGAIN : 0;
}


static struct snd_soc_platform_driver sunplus_soc_platform = {
	.ops			= &spsoc_pcm_ops,
	.pcm_new	= spsoc_pcm_new,
	.pcm_free	= spsoc_pcm_free_dma_buffers,
};

struct file_operations aud_f_ops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = spsoc_pcm_ioctl,
	.mmap = spsoc_reg_mmap,
};

void audfops_init(void)
{
	  cdev_init(&spaud_fops_cdev, &aud_f_ops);

	  if (cdev_add(&spaud_fops_cdev, MKDEV(spaud_fops_MAJOR, 0), 1) ||
	      register_chrdev_region(MKDEV(spaud_fops_MAJOR, 0), 1, "/dev/spaud_fops") < 0)
		AUD_ERR("fail to register '/dev/spaud_fops' for file operations\n");

	  device_create(sound_class, NULL, MKDEV(spaud_fops_MAJOR, 0), NULL, "spaud_fops");

	  AUD_INFO("audfops_init '/dev/spaud_fops' OK\n");
}
static  int preallocate_dma_buffer(struct platform_device *pdev)
{
	  unsigned int size;
	  
	  aud_param.fifoInfo.pcmtx_virtAddrBase=0;
	  aud_param.fifoInfo.mic_virtAddrBase=0;
    size = DRAM_PCM_BUF_LENGTH*(NUM_FIFO_TX + NUM_FIFO_RX);
    aud_param.fifoInfo.TxBuf_TotalLen = size;
#ifdef USE_KELNEL_MALLOC
	  aud_param.fifoInfo.pcmtx_virtAddrBase = (unsigned int)dma_alloc_coherent(NULL, PAGE_ALIGN(size), \
							&aud_param.fifoInfo.pcmtx_physAddrBase , GFP_DMA | GFP_KERNEL);
#else
	  aud_param.fifoInfo.pcmtx_virtAddrBase = (unsigned int)gp_chunk_malloc_nocache(1,0,PAGE_ALIGN(size));
	  aud_param.fifoInfo.pcmtx_physAddrBase = gp_chunk_pa( (void *)aud_param.fifoInfo.pcmtx_virtAddrBase );
#endif
    AUD_INFO("pcmtx_virtAddrBase 0x%x pcmtx_physAddrBase 0x%x\n", aud_param.fifoInfo.pcmtx_virtAddrBase, aud_param.fifoInfo.pcmtx_physAddrBase);
	  if(!aud_param.fifoInfo.pcmtx_virtAddrBase){
		    AUD_INFO("failed to allocate playback DMA memory\n");
		    return -ENOMEM;
	  }
	  
	  aud_param.fifoInfo.mic_virtAddrBase = aud_param.fifoInfo.pcmtx_virtAddrBase + DRAM_PCM_BUF_LENGTH*NUM_FIFO_TX;
	  aud_param.fifoInfo.mic_physAddrBase = aud_param.fifoInfo.pcmtx_physAddrBase + DRAM_PCM_BUF_LENGTH*NUM_FIFO_TX;
#if 0
	  size = DRAM_PCM_BUF_LENGTH*NUM_FIFO_RX;//*(NUM_FIFO_TX-1) + DRAM_HDMI_BUF_LENGTH;
	  aud_param.fifoInfo.RxBuf_TotalLen = size;
#ifdef USE_KELNEL_MALLOC
	  aud_param.fifoInfo.mic_virtAddrBase = (unsigned int)dma_alloc_coherent(NULL, PAGE_ALIGN(size), \
							&aud_param.fifoInfo.mic_physAddrBase , GFP_DMA | GFP_KERNEL);
#else
	  aud_param.fifoInfo.mic_virtAddrBase = (unsigned int)gp_chunk_malloc_nocache(1,0,PAGE_ALIGN(size));
	  aud_param.fifoInfo.mic_physAddrBase = gp_chunk_pa( (void *)aud_param.fifoInfo.mic_virtAddrBase );
#endif
    AUD_INFO("mic_virtAddrBase 0x%x mic_physAddrBase 0x%x\n", aud_param.fifoInfo.mic_virtAddrBase, aud_param.fifoInfo.mic_physAddrBase);
	  if(!aud_param.fifoInfo.mic_virtAddrBase){
		    AUD_INFO("failed to allocate  record DMA memory\n");
		    return -ENOMEM;
	  }
#endif
	  return 0;
}

static void dma_free_dma_buffers(struct platform_device *pdev)
{
	  unsigned int size;

	  size = DRAM_PCM_BUF_LENGTH*NUM_FIFO_TX;
#ifdef USE_KELNEL_MALLOC
		dma_free_coherent(NULL, size ,
				  (unsigned int *)aud_param.fifoInfo.pcmtx_virtAddrBase, aud_param.fifoInfo.pcmtx_physAddrBase);
#else
		//gp_chunk_free( (void *)aud_param.fifoInfo.pcmtx_virtAddrBase);
#endif

	  size = DRAM_PCM_BUF_LENGTH*NUM_FIFO_RX;
#ifdef USE_KELNEL_MALLOC
	  dma_free_coherent(NULL, size ,
					(unsigned int *)aud_param.fifoInfo.mic_virtAddrBase, aud_param.fifoInfo.mic_physAddrBase);
#else
			//gp_chunk_free((void*)aud_param.fifoInfo.mic_virtAddrBase);
#endif
}

static int __devinit snd_spsoc_probe(struct platform_device *pdev)
{
	  int ret = 0;
	  
	  AUD_INFO("%s IN\n",__func__);
	  ret = devm_snd_soc_register_platform(&pdev->dev, &sunplus_soc_platform);
	  // create & register device for file operation, used for 'ioctl'
	  //audfops_init();

	  memset(&aud_param,0, sizeof(struct t_auddrv_param));
	  memset(&aud_param.fifoInfo ,0, sizeof(struct t_AUD_FIFO_PARAMS));
	  memset(&aud_param.gainInfo ,0, sizeof(struct t_AUD_GAIN_PARAMS));
	  memset(&aud_param.fsclkInfo ,0, sizeof(struct t_AUD_FSCLK_PARAMS));
	  memset(&aud_param.i2scfgInfo ,0, sizeof(struct t_AUD_I2SCFG_PARAMS));

	  aud_param.fsclkInfo.freq_mask = 0x0667;	//192K
	  ret=preallocate_dma_buffer(pdev);

	  return ret;
}

static int __devexit snd_spsoc_remove(struct platform_device *pdev)
{
	  snd_soc_unregister_platform(&pdev->dev);
	  dma_free_dma_buffers(pdev);
	  return 0;
}

static struct platform_driver snd_spsoc_driver = {
	.driver = {
			.name = "spsoc-pcm-driver",
			.owner = THIS_MODULE,
	},

	.probe = snd_spsoc_probe,
	.remove = __devexit_p(snd_spsoc_remove),
};

#if 0	// for kernel 3.4.5
module_platform_driver(snd_spsoc_driver);
#else
static struct platform_device *spsoc_pcm_device;

static int __init snd_spsoc_pcm_init(void)
{
	  int ret= 0;

	  ret = platform_driver_register(&snd_spsoc_driver);
	  //AUD_INFO("register pcm driver for platform(spcoc_pcm):: %d\n", ret);
	  AUD_INFO("%s IN, create soc_card\n", __func__);

	  //zjg
	  spsoc_pcm_device = platform_device_alloc("spsoc-pcm-driver", -1);
	  if (!spsoc_pcm_device)
		    return -ENOMEM;

	  ret = platform_device_add(spsoc_pcm_device);
	  if (ret)
		    platform_device_put(spsoc_pcm_device);

	  return ret;
}
module_init(snd_spsoc_pcm_init);

static void __exit snd_spsoc_pcm_exit(void)
{
	  platform_driver_unregister(&snd_spsoc_driver);
}
module_exit(snd_spsoc_pcm_exit);
#endif

MODULE_AUTHOR("Sunplus DSP");
MODULE_DESCRIPTION("S+ SoC ALSA PCM module");
MODULE_LICENSE("GPL");
