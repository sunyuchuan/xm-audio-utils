//
//  ff_encoder_ios_hw.m
//  IJKMediaFramework
//
//  Created by Layne on 2018/3/13.
//  Copyright © 2018年 bilibili. All rights reserved.
//
#include "audio_encoder_ios_hw.h"
#include "mybufferqueue.h"
#include "libavutil/frame.h"
#include "libavutil/imgutils.h"
#include "libavcodec/avcodec.h"

#include <pthread.h>
#import <VideoToolbox/VideoToolbox.h>
#import <AudioToolbox/AudioToolbox.h>

typedef struct ATContext {
    AudioConverterRef m_converter;
    int64_t           pts;
    int64_t           nb_samples;
    BufQueue          queue;
    BufQueue          used_queue;
} ATContext;

typedef struct VTContext {
    VTCompressionSessionRef session;
    BufQueue                pkt_list;
    pthread_mutex_t         mutex;
    int32_t                 frame_count;
    int32_t                 gop_size;
    int32_t                 frame_rate;
    int32_t                 width;
    int32_t                 height;
    int32_t                 pix_fmt;
    int32_t                 raw_buf_size;
    boolean_t               has_b_frames;
    //for dump
    //FILE*                   fp;
} VTContext;

typedef struct Encoder_Opaque {
    int               frame_byte_size;
    int               flush_encoder;
    ATContext         *at_ctx;
    VTContext         *vt_ctx;
    FF_MediaType    type;
} Encoder_Opaque;

#pragma mark -- AudioCallBack
static OSStatus inputDataProc(AudioConverterRef inConverter, UInt32 *ioNumberDataPackets, AudioBufferList *ioData, AudioStreamPacketDescription **outDataPacketDescription, void *inUserData) {
    Encoder *ctx = inUserData;
    Encoder_Opaque *opaque = ctx->opaque;
    //AudioConverterFillComplexBuffer 编码过程中，会要求这个函数来填充输入数据，也就是原始PCM数据
    //null buffer flush encoder
    if (!opaque->at_ctx->queue.available) {
        if (opaque->flush_encoder) {
            *ioNumberDataPackets = 0;
            NSLog(@"end of stream flush encoder");
            return 0;
        } else {
            NSLog(@"no data wait.....");
            *ioNumberDataPackets = 0;
            return 1;
        }
    }
    ioData->mNumberBuffers = 1;
    ioData->mBuffers[0].mNumberChannels = 2;
    //dequeue data
    AVFrame *frame = bufqueue_get(&opaque->at_ctx->queue);
    ioData->mBuffers[0].mData = frame->data[0];
    ioData->mBuffers[0].mDataByteSize = frame->linesize[0];
    //put the frame to used queue
    bufqueue_add(&opaque->at_ctx->used_queue, frame);
    return noErr;
}

static int cm_to_avpacket(void *ctx, CMSampleBufferRef sample_buffer, AVPacket *pkt, BOOL isKeyFrame) {
    uint8_t header[] = {0x00, 0x00, 0x00, 0x01};
    NSMutableData *data = [[NSMutableData alloc] init];
    OSStatus statusCode = -1;
    //when meet a key frame, add sps/pps to header
    if (isKeyFrame) {
        CMFormatDescriptionRef format = CMSampleBufferGetFormatDescription(sample_buffer);

        size_t sparameterSetSize, sparameterSetCount;
        const uint8_t *sparameterSet;
        statusCode = CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format, 0, &sparameterSet, &sparameterSetSize, &sparameterSetCount, 0);
        if (statusCode == noErr) {
            size_t pparameterSetSize, pparameterSetCount;
            const uint8_t *pparameterSet;
            statusCode = CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format, 1, &pparameterSet, &pparameterSetSize, &pparameterSetCount, 0);
            if (statusCode == noErr) {
                [data appendBytes:header length:4];
                [data appendBytes:sparameterSet length:sparameterSetSize];
                [data appendBytes:header length:4];
                [data appendBytes:pparameterSet length:pparameterSetSize];
                //av_log(NULL, AV_LOG_INFO, "key frame write sps size %lu, pps size %lu\n", sparameterSetSize, pparameterSetSize);
                //fwrite(data.bytes, 1, data.length, ((VTContext *)ctx)->fp);
            }
        }
    }

    CMBlockBufferRef dataBuffer = CMSampleBufferGetDataBuffer(sample_buffer);
    size_t length, totalLength;
    char *dataPointer;
    statusCode = CMBlockBufferGetDataPointer(dataBuffer, 0, &length, &totalLength, &dataPointer);
    if (statusCode == noErr) {
        size_t bufferOffset = 0;
        static const int AVCCHeaderLength = 4;
        while (bufferOffset < totalLength - AVCCHeaderLength) {
            // Read the NAL unit length
            uint32_t NALUnitLength = 0;
            memcpy(&NALUnitLength, dataPointer + bufferOffset, AVCCHeaderLength);
            NALUnitLength = CFSwapInt32BigToHost(NALUnitLength);
            [data appendBytes:header length:4];
            [data appendBytes:dataPointer + bufferOffset + AVCCHeaderLength length:NALUnitLength];
            bufferOffset += AVCCHeaderLength + NALUnitLength;
            //fwrite(data.bytes, 1, data.length, ((VTContext *)ctx)->fp);
        }
    }
    int ret = av_new_packet(pkt, (int)data.length);
    if (ret < 0) {
        return ret;
    }
    //av_log(NULL, AV_LOG_INFO, "packet size %d\n", pkt->size);
    memcpy(pkt->data, data.bytes, data.length);
    return 0;
}

static int copy_avframe_to_pixel_buffer(void *ctx,
                                        const AVFrame    *frame,
                                        CVPixelBufferRef cv_img,
                                        const size_t     *plane_strides,
                                        const size_t     *plane_rows)
{
    int i, j;
    size_t plane_count;
    int status = 0;
    size_t rows;
    size_t src_stride;
    size_t dst_stride;
    uint8_t *src_addr;
    uint8_t *dst_addr;
    size_t copy_bytes;

    status = CVPixelBufferLockBaseAddress(cv_img, 0);
    if (status) {
        NSLog(@"Error: Could not lock base address of CVPixelBuffer: %d.\n", status);
    }

    if (CVPixelBufferIsPlanar(cv_img)) {
        plane_count = CVPixelBufferGetPlaneCount(cv_img);
        for (i = 0; frame->data[i]; i++) {
            if (i == plane_count) {
                CVPixelBufferUnlockBaseAddress(cv_img, 0);
                NSLog(@"Error: different number of planes in AVFrame and CVPixelBuffer.\n");
                return AVERROR_EXTERNAL;
            }

            dst_addr = (uint8_t*)CVPixelBufferGetBaseAddressOfPlane(cv_img, i);
            src_addr = (uint8_t*)frame->data[i];
            dst_stride = CVPixelBufferGetBytesPerRowOfPlane(cv_img, i);
            src_stride = plane_strides[i];
            rows = plane_rows[i];

            if (dst_stride == src_stride) {
                memcpy(dst_addr, src_addr, src_stride * rows);
            } else {
                copy_bytes = dst_stride < src_stride ? dst_stride : src_stride;
                for (j = 0; j < rows; j++) {
                    memcpy(dst_addr + j * dst_stride, src_addr + j * src_stride, copy_bytes);
                }
            }
        }
    } else {
        if (frame->data[1]) {
            CVPixelBufferUnlockBaseAddress(cv_img, 0);
            NSLog(@"Error: different number of planes in AVFrame and non-planar CVPixelBuffer.");
            return AVERROR_EXTERNAL;
        }

        dst_addr = (uint8_t*)CVPixelBufferGetBaseAddress(cv_img);
        src_addr = (uint8_t*)frame->data[0];
        dst_stride = CVPixelBufferGetBytesPerRow(cv_img);
        src_stride = plane_strides[0];
        rows = plane_rows[0];

        if (dst_stride == src_stride) {
            memcpy(dst_addr, src_addr, src_stride * rows);
        } else {
            copy_bytes = dst_stride < src_stride ? dst_stride : src_stride;
            for (j = 0; j < rows; j++) {
                memcpy(dst_addr + j * dst_stride, src_addr + j * src_stride, copy_bytes);
            }
        }
    }

    status = CVPixelBufferUnlockBaseAddress(cv_img, 0);
    if (status) {
        NSLog(@"Error: Could not unlock CVPixelBuffer base address: %d.", status);
        return AVERROR_EXTERNAL;
    }

    return 0;
}

static int create_cv_pixel_buffer(void *ctx, const AVFrame *frame, CVPixelBufferRef *cv_img) {
    int ret = -1;
    VTContext *vt = (VTContext *)ctx;
    CVPixelBufferPoolRef pix_buf_pool;
    size_t widths [AV_NUM_DATA_POINTERS];
    size_t heights[AV_NUM_DATA_POINTERS];
    size_t strides[AV_NUM_DATA_POINTERS];

    switch (vt->pix_fmt) {
        case AV_PIX_FMT_NV12:

            widths [0] = vt->width;
            heights[0] = vt->height;
            strides[0] = frame ? frame->linesize[0] : vt->width;

            widths [1] = (vt->width  + 1) / 2;
            heights[1] = (vt->height + 1) / 2;
            strides[1] = frame ? frame->linesize[1] : (vt->width + 1) & -2;
            break;

        case AV_PIX_FMT_YUV420P:
            widths [0] = vt->width;
            heights[0] = vt->height;
            strides[0] = frame ? frame->linesize[0] : vt->width;

            widths [1] = (vt->width  + 1) / 2;
            heights[1] = (vt->height + 1) / 2;
            strides[1] = frame ? frame->linesize[1] : (vt->width + 1) / 2;

            widths [2] = (vt->width  + 1) / 2;
            heights[2] = (vt->height + 1) / 2;
            strides[2] = frame ? frame->linesize[2] : (vt->width + 1) / 2;
            break;
            
        default:
            av_log(NULL, AV_LOG_ERROR, "unsupport color format !!!! \n");
            return AVERROR(EINVAL);
    }

    pix_buf_pool = VTCompressionSessionGetPixelBufferPool(vt->session);
    if (!pix_buf_pool) {
        NSLog(@"Could not get pixel buffer pool.");
        return AVERROR_EXTERNAL;
    }

    ret = CVPixelBufferPoolCreatePixelBuffer(NULL, pix_buf_pool, cv_img);
    if (ret) {
        NSLog(@"Could not create pixel buffer from pool: %d.", ret);
        return AVERROR_EXTERNAL;
    }

    ret = copy_avframe_to_pixel_buffer(ctx, frame, *cv_img, strides, heights);
    if (ret) {
        CFRelease(*cv_img);
        *cv_img = NULL;
        NSLog(@"can't create CVPixelBufferRef !!!!!");
        return ret;
    }
    return ret;
}

#pragma mark -- VideoCallBack
static void VideoCompressonOutputCallback(void *VTref, void *VTFrameRef, OSStatus status, VTEncodeInfoFlags infoFlags, CMSampleBufferRef sampleBuffer) {
    @autoreleasepool {
        VTContext *vt_ctx = (VTContext *)VTref;

        if (!sampleBuffer)return;

        CFArrayRef array = CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, true);
        if (!array) return;

        CFDictionaryRef dic = (CFDictionaryRef)CFArrayGetValueAtIndex(array, 0);
        if (!dic) return;

        BOOL keyframe = !CFDictionaryContainsKey(dic, kCMSampleAttachmentKey_NotSync);
        //uint64_t timeStamp = [((__bridge_transfer NSNumber *)VTFrameRef) longLongValue];
        CMTime pts = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
        CMTime dts = CMSampleBufferGetDecodeTimeStamp(sampleBuffer);

        if (CMTIME_IS_INVALID(dts)) {
            if (!vt_ctx->has_b_frames) {
                dts = pts;
            } else {
                NSLog(@"dts is invalid.\n");
                return;
            }
        }
        AVPacket *pkt = av_packet_alloc();
        cm_to_avpacket(vt_ctx, sampleBuffer, pkt, keyframe);
        if (keyframe)
            pkt->flags |= AV_PKT_FLAG_KEY;
        pkt->pts = pts.value;
        pkt->dts = dts.value;
        //NSLog(@"timeStamp %lld out pkt pts %lld, dts %lld", timeStamp, pkt->pts, pkt->dts);
        //CFRelease(sampleBuffer);
        if (pkt->size != 0) {
            pthread_mutex_lock(&vt_ctx->mutex);
            if (!bufqueue_is_full(&vt_ctx->pkt_list)) {
                bufqueue_add(&vt_ctx->pkt_list, pkt);
            } else {
                NSLog(@"pkt list is too small\n");
            }
            pthread_mutex_unlock(&vt_ctx->mutex);
        }
    }
}

static int ios_hw_encoder_config(Encoder *ctx, AVDictionary *opt) {
    OSStatus ret = -1;
    Encoder_Opaque *opaque = ctx->opaque;
    AVDictionaryEntry *e = NULL;

    //find codec type
    while ((e = av_dict_get(opt, "", e, AV_DICT_IGNORE_SUFFIX)) != NULL) {
        if (!strcasecmp(e->key, "mime")) {
            if (!strcasecmp(e->value, MIME_VIDEO_AVC)) {
                opaque->type = FF_AVMEDIA_TYPE_VIDEO;
                break;
            } else if (!strcasecmp(e->value, MIME_AUDIO_AAC)) {
                opaque->type = FF_AVMEDIA_TYPE_AUDIO;
                break;
            } else {
                opaque->type = FF_AVMEDIA_TYPE_UNKNOWN;
                NSLog(@"unsupport mime %s!\n", e->value);
                return ret;
            }
        }
    }

    if (opaque->type == FF_AVMEDIA_TYPE_AUDIO) {
        //audio encoder config
        int bit_rate = 0;
        int sample_rate = 0;
        int channels = 0;
        int64_t channel_layout = 0;
        AudioConverterRef audioconverter;
        while ((e = av_dict_get(opt, "", e, AV_DICT_IGNORE_SUFFIX)) != NULL) {
            if (!strcasecmp(e->key, "bit_rate")) {
                bit_rate = atoi(e->value);
            } else if (!strcasecmp(e->key, "sample_rate")) {
                sample_rate = atoi(e->value);
            } else if (!strcasecmp(e->key, "channels")) {
                channels = atoi(e->value);
            } else if (!strcasecmp(e->key, "channel_layout")) {
                channel_layout = atol(e->value);
            }
        }

        AudioStreamBasicDescription in_format = {
            .mSampleRate = sample_rate,
            .mFormatID = kAudioFormatLinearPCM,
            .mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked,
            .mBytesPerPacket = 2 * 2,
            .mFramesPerPacket = 1,
            .mBytesPerFrame = 2 * 2,
            .mChannelsPerFrame = channels,
            .mBitsPerChannel = 16,
        };
        AudioStreamBasicDescription out_format = {
            .mSampleRate = sample_rate,
            .mFormatID = kAudioFormatMPEG4AAC,
            .mChannelsPerFrame = in_format.mChannelsPerFrame,
        };

        NSLog(@"audio toolbox channels %d bit_rate %d sample_rate %d\n", channels, bit_rate, sample_rate);
        const OSType subtype = kAudioFormatMPEG4AAC;
        AudioClassDescription requestedCodecs[2] = {
            {
                kAudioEncoderComponentType,
                subtype,
                kAppleSoftwareAudioCodecManufacturer
            },
            {
                kAudioEncoderComponentType,
                subtype,
                kAppleHardwareAudioCodecManufacturer
            }
        };

        ret = AudioConverterNewSpecific(&in_format, &out_format, 2, requestedCodecs, &audioconverter);

        //set input/output channel layout
        /*if (ret == noErr) {
            UInt32 layout_size = sizeof(AudioChannelLayout) + sizeof(AudioChannelDescription);
            AudioChannelLayout *channel_layout = av_malloc(layout_size);
            ret = AudioConverterSetProperty(audioconvert, kAudioConverterInputChannelLayout,layout_size, channel_layout);
            av_free(channel_layout);
        }*/
        //set output bitrate
        UInt32 propSize = sizeof(bit_rate);
        if (bit_rate > 0) {
            UInt32 rate = bit_rate;
            ret = AudioConverterGetPropertyInfo(audioconverter,
                                                kAudioConverterApplicableEncodeBitRates,
                                                &propSize, NULL);
            if (!ret && propSize) {
                UInt32 new_rate = rate;
                int count;
                int i;
                AudioValueRange *ranges = av_malloc(propSize);
                if (!ranges)
                    return AVERROR(ENOMEM);
                AudioConverterGetProperty(audioconverter,
                                          kAudioConverterApplicableEncodeBitRates,
                                          &propSize, ranges);
                count = propSize / sizeof(AudioValueRange);
                for (i = 0; i < count; i++) {
                    AudioValueRange *range = &ranges[i];
                    if (rate >= range->mMinimum && rate <= range->mMaximum) {
                        new_rate = rate;
                        break;
                    } else if (rate > range->mMaximum) {
                        new_rate = range->mMaximum;
                    } else {
                        new_rate = range->mMinimum;
                        break;
                    }
                }
                if (new_rate != rate) {
                    NSLog(@"Bitrate %u not allowed; changing to %u\n", (unsigned int)rate, (unsigned int)new_rate);
                    rate = new_rate;
                }
                av_free(ranges);
            }
            AudioConverterSetProperty(audioconverter, kAudioConverterEncodeBitRate, sizeof(rate), &rate);
        }
        propSize = sizeof(out_format);
        if (ret == noErr) {
            ret = AudioConverterGetProperty(audioconverter, kAudioConverterCurrentOutputStreamDescription, &propSize, &out_format);
            NSLog(@"frame size %d\n", (int)out_format.mFramesPerPacket);
        }
        opaque->frame_byte_size = in_format.mBytesPerPacket * out_format.mFramesPerPacket;
        opaque->at_ctx = (ATContext *)av_mallocz(sizeof(ATContext));
        opaque->at_ctx->m_converter = audioconverter;
        opaque->at_ctx->nb_samples  = out_format.mFramesPerPacket;
        opaque->at_ctx->queue.free_node = (void (*)(void **))av_frame_free;
        opaque->at_ctx->used_queue.free_node = (void (*)(void **))av_frame_free;
    }  else if(opaque->type == FF_AVMEDIA_TYPE_VIDEO) {
        //video encoder config
        uint64_t bit_rate = 0;
        int width = 0;
        int height = 0;
        int gop_size = 125;
        int pix_fmt = AV_PIX_FMT_NV12;
        int framerate = 25;
        while ((e = av_dict_get(opt, "", e, AV_DICT_IGNORE_SUFFIX)) != NULL) {
            if (!strcasecmp(e->key, "bit_rate")) {
                bit_rate = atoll(e->value);
            } else if (!strcasecmp(e->key, "width")) {
                width = atoi(e->value);
            } else if (!strcasecmp(e->key, "height")) {
                height = atoi(e->value);
            } else if (!strcasecmp(e->key, "gop_size")) {
                gop_size = atoi(e->value);
            } else if (!strcasecmp(e->key, "pix_format")) {
                pix_fmt = atoi(e->value);
            } else if (!strcasecmp(e->key, "framerate")) {
                framerate = atoi(e->value);
            }
        }
        opaque->vt_ctx = (VTContext *)av_mallocz(sizeof(VTContext));
        opaque->vt_ctx->gop_size = gop_size;
        opaque->vt_ctx->frame_rate= framerate;
        opaque->vt_ctx->width = width;
        opaque->vt_ctx->height = height;
        opaque->vt_ctx->pix_fmt = pix_fmt;
        opaque->vt_ctx->raw_buf_size = av_image_get_buffer_size(pix_fmt, width, height, 16);
        opaque->vt_ctx->pkt_list.free_node = (void (*)(void **))av_packet_free;
        opaque->vt_ctx->has_b_frames = false;
        opaque->frame_byte_size = opaque->vt_ctx->raw_buf_size;
        pthread_mutex_init(&opaque->vt_ctx->mutex, NULL);
        VTCompressionSessionRef session = NULL;
        OSType color_fmt = 0;
        if (pix_fmt == AV_PIX_FMT_YUV420P) {
            color_fmt = kCVPixelFormatType_420YpCbCr8Planar;
        } else if (pix_fmt == AV_PIX_FMT_NV12) {
            color_fmt = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
        } else {
            NSLog(@"unsupport color format\n");
            return -1;
        }
        CFMutableDictionaryRef imgAttr = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFNumberRef number = CFNumberCreate(NULL, kCFNumberSInt32Type, &color_fmt);
        CFDictionarySetValue(imgAttr, kCVPixelBufferPixelFormatTypeKey, number);
        CFRelease (number);
        ret = VTCompressionSessionCreate(kCFAllocatorDefault,
                                         width,
                                         height,
                                         kCMVideoCodecType_H264,
                                         NULL,
                                         imgAttr,
                                         NULL,
                                         VideoCompressonOutputCallback,
                                         opaque->vt_ctx,
                                         &session);

        if (ret || !session) {
            NSLog(@"Error: cannot create compression session: %d\n", (int)ret);
            return ret;
        }

        VTSessionSetProperty(session, kVTCompressionPropertyKey_MaxKeyFrameInterval, (__bridge CFTypeRef)@(gop_size));
        VTSessionSetProperty(session, kVTCompressionPropertyKey_MaxKeyFrameIntervalDuration, (__bridge CFTypeRef)@(gop_size / framerate));
        VTSessionSetProperty(session, kVTCompressionPropertyKey_ExpectedFrameRate, (__bridge CFTypeRef)@(framerate));
        VTSessionSetProperty(session, kVTCompressionPropertyKey_AverageBitRate, (__bridge CFTypeRef)@(bit_rate));
        NSArray *limit = @[@(bit_rate * 1.5/8), @(1)];
        VTSessionSetProperty(session, kVTCompressionPropertyKey_DataRateLimits, (__bridge CFArrayRef)limit);
        VTSessionSetProperty(session, kVTCompressionPropertyKey_RealTime, kCFBooleanTrue);
        VTSessionSetProperty(session, kVTCompressionPropertyKey_ProfileLevel, kVTProfileLevel_H264_High_AutoLevel);
        VTSessionSetProperty(session, kVTCompressionPropertyKey_AllowFrameReordering, kCFBooleanTrue);
        VTSessionSetProperty(session, kVTCompressionPropertyKey_H264EntropyMode, kVTH264EntropyMode_CABAC);
        ret = VTCompressionSessionPrepareToEncodeFrames(session);
        if (ret < 0) {
            NSLog(@"prepare compression error %d \n", (int)ret);
        }
        CFBooleanRef has_b_frames_cfbool;
        ret = VTSessionCopyProperty(session, kVTCompressionPropertyKey_AllowFrameReordering, kCFAllocatorDefault, &has_b_frames_cfbool);
        if (!ret) {
            //some devices don't output b-frames for main profile,even if requested
            //stole from ffmpeg
            opaque->vt_ctx->has_b_frames = CFBooleanGetValue(has_b_frames_cfbool);
            CFRelease(has_b_frames_cfbool);
        }
        NSLog(@"video toolbox width %d height %d bit_rate %lld gop size %d fps %d\n", width, height, bit_rate, gop_size, framerate);
        NSLog(@"have b frames %d\n", opaque->vt_ctx->has_b_frames);
        opaque->vt_ctx->session = session;
#if 0
        NSArray *paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
        NSString *outavc = [[paths objectAtIndex:0] stringByAppendingString:@"/out.h264"];
        NSFileManager *manager = [NSFileManager defaultManager];
        NSError *err = nil;
        [manager removeItemAtPath:outavc error:&err];
        opaque->vt_ctx->fp = fopen(outavc.UTF8String, "wb");
#endif
        CFRelease(imgAttr);
    }

    return ret;
}

static int ios_hw_encoder_get_frame_size(Encoder *ctx) {
    Encoder_Opaque *opaque = ctx->opaque;
    NSLog(@"ios hw frame size %d \n", opaque->frame_byte_size);
    return opaque->frame_byte_size;
}

static int ios_hw_encoder_encode_frame(Encoder *ctx, AVFrame *frame, AVPacket *pkt, int *got_packet_ptr) {
    Encoder_Opaque *opaque = ctx->opaque;
    OSStatus ret = 0;

    if (opaque->type == FF_AVMEDIA_TYPE_AUDIO) {
        if (frame) {
            //put frame in queue
            if (bufqueue_is_full(&opaque->at_ctx->queue)) {
                NSLog(@"frame queue is too small");
                return -1;
            }
            //av_log(NULL, AV_LOG_INFO, "add frame to queue\n");
            bufqueue_add(&opaque->at_ctx->queue, av_frame_clone(frame));
        } else {
            NSLog(@"end of stream");
            //audiotoolbox no need,just call encoder function
            opaque->flush_encoder = 1;
        }

        AudioStreamPacketDescription out_pkt_desc = { 0 };
        //pkt->data = av_mallocz(4096);
        ret = av_new_packet(pkt, 4096);
        if (ret < 0) {
            return ret;
        }
        // 初始化一个输出缓冲列表
        AudioBufferList outBufferList;
        outBufferList.mNumberBuffers = 1;
        outBufferList.mBuffers[0].mNumberChannels = 2;
        outBufferList.mBuffers[0].mDataByteSize = 4096;
        outBufferList.mBuffers[0].mData = pkt->data;

        UInt32 outputDataPacketSize = 1;
        ret = AudioConverterFillComplexBuffer(opaque->at_ctx->m_converter, inputDataProc, ctx, &outputDataPacketSize, &outBufferList, &out_pkt_desc);
        bufqueue_discard_all(&opaque->at_ctx->used_queue);
        if (ret == noErr) {
            if (outputDataPacketSize > 0) {
                *got_packet_ptr = 1;
                pkt->pts = opaque->at_ctx->pts;
                opaque->at_ctx->pts += opaque->at_ctx->nb_samples;
                pkt->size = outBufferList.mBuffers[0].mDataByteSize;
                //av_log(NULL, AV_LOG_INFO, "out put buffer size %d ret %d pts %"PRId64"\n", pkt->size, ret, pkt->pts);
            } else {
                *got_packet_ptr = 0;
                NSLog(@"no pkt out maybe end of stream");
            }
        } else {
            *got_packet_ptr = 0;
            ret = -1;
            av_packet_unref(pkt);
            NSLog(@"Encode error %s", strerror(ret));
        }
    } else if (opaque->type == FF_AVMEDIA_TYPE_VIDEO) {
        if (frame) {
            CVPixelBufferRef cv_img = NULL;
            ret = create_cv_pixel_buffer(opaque->vt_ctx, frame, &cv_img);

            if (ret != noErr) {
                NSLog(@"create cvpixelbuffer error %d", (int)ret);
                return ret;
            }

            CMTime presentationTimeStamp = CMTimeMake(frame->pts, opaque->vt_ctx->frame_rate);
            //CMTime duration = CMTimeMake(1, opaque->vt_ctx->frame_rate);
            //NSNumber *timeNumber = @(frame->pts);

            NSDictionary *properties = nil;
            if (opaque->vt_ctx->frame_count % (int32_t)(opaque->vt_ctx->gop_size) == 0) {
                properties = @{(__bridge NSString *)kVTEncodeFrameOptionKey_ForceKeyFrame: @YES};
            }
            opaque->vt_ctx->frame_count++;
            //ret = VTCompressionSessionEncodeFrame(opaque->vt_ctx->session, cv_img, presentationTimeStamp, duration, (__bridge CFDictionaryRef)properties, (__bridge_retained void *)timeNumber, NULL);
            ret = VTCompressionSessionEncodeFrame(opaque->vt_ctx->session, cv_img, presentationTimeStamp, kCMTimeInvalid, (__bridge CFDictionaryRef)properties, NULL, NULL);
            if (ret != noErr) {
                NSLog(@"VTCompressionSessionEncodeFrame error %d", (int)ret);
                CFRelease(cv_img);
                return ret;
            }
            CFRelease(cv_img);
        } else if (!opaque->flush_encoder) {
            opaque->flush_encoder = 1;
            ret = VTCompressionSessionCompleteFrames(opaque->vt_ctx->session, kCMTimeIndefinite);

            if (ret) {
                NSLog(@"Error flushing frames: %d", (int)ret);
                return ret;
            }
        }
        pthread_mutex_lock(&opaque->vt_ctx->mutex);
        if (!bufqueue_is_empty(&opaque->vt_ctx->pkt_list)) {
            AVPacket *pkt0 = bufqueue_get(&opaque->vt_ctx->pkt_list);
            av_packet_ref(pkt, pkt0);
            av_packet_free(&pkt0);
            *got_packet_ptr = 1;
            //av_log(NULL, AV_LOG_INFO, "got a pkt size %d dts %lld pts %lld\n", pkt->size, pkt->dts, pkt->pts);
        } else {
            *got_packet_ptr = 0;
        }
        pthread_mutex_unlock(&opaque->vt_ctx->mutex);
    }

    return ret;
}

static int ios_hw_encoder_destroy(Encoder *ctx) {
    Encoder_Opaque *opauqe = ctx->opaque;
    if (opauqe->at_ctx) {
        bufqueue_discard_all(&opauqe->at_ctx->queue);
        bufqueue_discard_all(&opauqe->at_ctx->used_queue);
        NSLog(@"free queue and used_queue\n");
        if (opauqe->at_ctx->m_converter != NULL) {
            AudioConverterDispose(opauqe->at_ctx->m_converter);
            opauqe->at_ctx->m_converter = NULL;
            NSLog(@"AudioConverterDispose\n");
        }
        av_freep(&opauqe->at_ctx);
    }
    if (opauqe->vt_ctx) {
        if (opauqe->vt_ctx->session != NULL) {
            VTCompressionSessionCompleteFrames(opauqe->vt_ctx->session, kCMTimeInvalid);
            VTCompressionSessionInvalidate(opauqe->vt_ctx->session);
            CFRelease(opauqe->vt_ctx->session);
            opauqe->vt_ctx->session = NULL;
        }
        bufqueue_discard_all(&opauqe->vt_ctx->pkt_list);
        pthread_mutex_destroy(&opauqe->vt_ctx->mutex);
        av_freep(&opauqe->vt_ctx);
        //fclose(opauqe->vt_ctx->fp);
        NSLog(@"free videotoolbox context\n");
    }
    return 0;
}

Encoder *ff_encoder_ios_hw_create() {
    Encoder *encoder = ff_encoder_alloc(sizeof(Encoder_Opaque));
    if (!encoder)
        return encoder;

    Encoder_Opaque *opaque = encoder->opaque;

    opaque->type = FF_AVMEDIA_TYPE_UNKNOWN;

    encoder->func_config  = ios_hw_encoder_config;
    encoder->func_get_frame_size = ios_hw_encoder_get_frame_size;
    encoder->func_encode_frame = ios_hw_encoder_encode_frame;
    encoder->func_destroy = ios_hw_encoder_destroy;

    return encoder;
}
