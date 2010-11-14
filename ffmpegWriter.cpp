// Copyright (c) 2009 The Foundry Visionmongers Ltd.  All Rights Reserved.

#include "Build/fnBuild.h"
#include "DDImage/DDString.h"
#include "DDImage/Writer.h"
#include "DDImage/Row.h"
#include "DDImage/Knobs.h"
#include "DDImage/plugins.h"
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

extern "C" {
#include <dirent.h>
#include <errno.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/opt.h>
#include <libswscale/swscale.h>
#include <libavutil/avutil.h>
}

#if __WORDSIZE == 64
  #define INT64_C(c) c ## L
#else
  #define INT64_C(c) c ## LL
#endif

using namespace DD::Image;

class ffmpegWriter : public Writer
{
private:
  enum WriterError { SUCCESS = 0, IGNORE_FINISH, CLEANUP };

public:
  explicit ffmpegWriter(Write* iop);
  ~ffmpegWriter();

  virtual bool movie() const { return true; }

  void execute();
  void finish();
  void knobs(Knob_Callback f);
  static const Writer::Description d;

private:
  void freeFormat();
  bool isCodecBlacklisted(const char* name);
  int opt_default(const char *opt, const char *arg);

private:
  AVCodecContext* avctxOptions_[CODEC_TYPE_NB];
  AVFormatContext* avformatOptions_;
  AVStream* stream_;
  std::vector<std::string> formatsLongNames_;
  std::vector<const char*> formatsShortNames_;
  std::vector<const char*> codecsLongNames_;
  std::vector<const char*> codecsShortNames_;
  
  char ffmpegDataDir[1000];

  std::vector<std::string> ffmpegPresets;
  char ffPresetFilename[1000];
  char ffPresetName[1000];

  const char **opt_names;
  const char **opt_values;
  static int opt_name_count;
  struct SwsContext *sws_opts;

  WriterError error_;
  // knobs variables
  float fps_;
  int format_;
  int codec_;
  int bitrate_;
  int bitrateTolerance_;
  int mbDecision_;

  int ffPreset;
};

static const char* codecBlacklist[] =
{
#ifdef FN_OS_WINDOWS

  "dv",
  "ogg",
  "mpjpeg",
  "ipod",
  "psp",
  "asf",
  "asf_stream",
  "dirac",
  "ffm",
  "h261",
  "h263",
  "h264",
  "image2",
  "3g2",
  "3gp",
  "RoQ",
  "swf",
  "rcv",
  "yuv4mpegpipe",
  
#elif FN_OS_MAC

  "ogg",
  "mjpeg",
  "mpjpeg",
  "vc1",
  "dv",
  "ipod",
  "psp",
  "asf",
  "asf_stream",
  "h261",
  "h263",
  "h264",
  "image2",
  "RoQ",
  "swf",
  "3g2",
  "3gp",
  "rcv",
  "yuv4mpegpipe",

#elif FN_OS_LINUX
 
  "ogg",
  "mpjpeg",
  "vc1",
  "dv",
  "ipod",
  "psp",
  "asf",
  "asf_stream",
  "h261",
  "h263",
  "h264",
  "image2",
  "3g2",
  "3gp",
  "rcv",
  "yuv4mpegpipe",
  "RoQ",
  "swf",

#endif

  NULL

};


int ffmpegWriter::opt_default(const char *opt, const char *arg) {
  int type;
  int ret= 0;
  const AVOption *o= av_find_opt(stream_->codec, opt, NULL, 0, 0);

  int opt_types[]={AV_OPT_FLAG_VIDEO_PARAM};
  if(strcmp(opt, "bf")) {
    av_set_string3(stream_->codec, opt, arg, 1, &o);
  }

  //FIXME Continue from here

}

ffmpegWriter::ffmpegWriter(Write* iop)
  : Writer(iop)
  , avformatOptions_(0)
  , stream_(0)
  , error_(IGNORE_FINISH)
  , fps_(25.0f)
  , format_(0)
  , codec_(0)
  , bitrate_(4000000)
  , bitrateTolerance_(40000 * 10000)
  , mbDecision_(FF_MB_DECISION_SIMPLE)
{
  av_log_set_level(AV_LOG_WARNING);
  av_register_all();

  for (int i = 0; i < CODEC_TYPE_NB; ++i)
    avctxOptions_[i] = avcodec_alloc_context2(CodecType(i));

  int pluginPaths = (int)DD::Image::getPluginPath().size();
  for (int i = 0; i < pluginPaths; i++) {
    std::stringstream pluginPath;
    pluginPath << DD::Image::getPluginPath()[i] << "/ffPresets";

    if ( access( pluginPath.str().c_str(), 0 ) == 0 ) {

      struct stat status;
      stat( pluginPath.str().c_str(), &status );
      if ( status.st_mode & S_IFDIR ) {
        strcpy(ffmpegDataDir, pluginPath.str().c_str());
      }

    }
  }


  ffmpegPresets.push_back("default");
  if (ffmpegDataDir) {
    struct dirent *dp;

    DIR *dir = opendir(ffmpegDataDir);
    while ((dp=readdir(dir)) != NULL) {

      const char* filename = dp->d_name;

      if(strcmp(filename, ".") != 0 && strcmp(filename, "..") != 0) {
        if(strlen(filename) > sizeof(".ffpreset")) {
          char *extension = (char*) malloc(100);
          strncpy(extension, filename + strlen(filename) - strlen(".ffpreset"), strlen(".ffpreset")+1);
          if(strcmp(extension, ".ffpreset") == 0) {
            ffmpegPresets.push_back(filename);
          }
        }
      }
    }
    closedir(dir);
  }
  //snprintf(ffPresetFilename, sizeof(ffPresetFilename), "%s/test.preset", ffmpegDataDir);

  //printf("%s\n", ffPresetFilename);

  formatsLongNames_.push_back("default");
  formatsShortNames_.push_back("default");
  AVOutputFormat* fmt = av_oformat_next(NULL);
  while (fmt) {
   
    if (fmt->video_codec != CODEC_ID_NONE && !isCodecBlacklisted( fmt->name ) ) {
      if (fmt->long_name) {
        formatsLongNames_.push_back(std::string(fmt->long_name) + std::string(" (") + std::string(fmt->name) + std::string(")"));
        formatsShortNames_.push_back(fmt->name);
      }
    }
    fmt = av_oformat_next(fmt);
  }
  formatsShortNames_.push_back(0);

  codecsLongNames_.push_back("default");
  codecsShortNames_.push_back("default");
  AVCodec* c = av_codec_next(NULL);
  while (c) {
    if (c->type == CODEC_TYPE_VIDEO && c->encode && !isCodecBlacklisted( c->name ) ) {
      if (c->long_name) {
        codecsLongNames_.push_back(c->long_name);
        codecsShortNames_.push_back(c->name);
      }
    }
    c = av_codec_next(c);
  }
  codecsLongNames_.push_back(0);
  codecsShortNames_.push_back(0);
}

bool ffmpegWriter::isCodecBlacklisted(const char* name)
{
  const char** iterator = codecBlacklist;

  while( *iterator != NULL ) {
    if (strncmp(name, *iterator, strlen(*iterator)) == 0)  {
      return true;
    }

    ++iterator;
  }
  return false;
}

ffmpegWriter::~ffmpegWriter()
{
  for (int i = 0; i < CODEC_TYPE_NB; ++i)
    av_free(avctxOptions_[i]);
}

void ffmpegWriter::execute()
{
  error_ = IGNORE_FINISH;

  AVOutputFormat* fmt = 0;
  if (!format_) {
    fmt = av_guess_format(NULL, filename(), NULL);
    if (!fmt) {
      iop->error("could not deduce output format from file extension");
      return;
    }
  }
  else {
    fmt = av_guess_format(formatsShortNames_[format_], NULL, NULL);
    if (!fmt) {
      iop->error("could not deduce output format");
      return;
    }
  }

  if (!avformatOptions_)
    avformatOptions_ =  avformat_alloc_context();

  avformatOptions_->oformat = fmt;
  snprintf(avformatOptions_->filename, sizeof(avformatOptions_->filename), "%s", filename());

  if (!stream_) {
    stream_ = av_new_stream(avformatOptions_, 0);
    if (!stream_) {
      iop->error("out of memory");
      return;
    }

    CodecID codecId = fmt->video_codec;
    if (codec_) {
      AVCodec* userCodec = avcodec_find_encoder_by_name(codecsShortNames_[codec_]);
      if (userCodec)
        codecId = userCodec->id;
    }
    
    AVCodec* videoCodec = avcodec_find_encoder(codecId);
    if (!videoCodec) {
      iop->error("unable to find codec");
      freeFormat();
      return;
    }

    stream_->codec->codec_id = codecId;
    stream_->codec->codec_type = CODEC_TYPE_VIDEO;
    stream_->codec->bit_rate = bitrate_;
    stream_->codec->bit_rate_tolerance = bitrateTolerance_;
    stream_->codec->width = width();
    stream_->codec->height = height();
    stream_->codec->time_base = av_d2q(1.0 / fps_, 100);

    strcpy(ffPresetName, ffmpegPresets[ffPreset].c_str());

    if(strcmp(ffPresetName, "default")) {

      sprintf(ffPresetFilename, "%s/%s", ffmpegDataDir, ffPresetName);

      FILE *f;
      f = fopen(ffPresetFilename, "r");
      if(!f){
        iop->error("File for preset '%s' not found\n", ffPresetFilename);
        return;
      }

      char line[1000], tmp[1000], tmp2[1000];
      while(!feof(f)){
        int e= fscanf(f, "%999[^\n]\n", line) - 1;
        if(line[0] == '#' && !e)
          continue;
        e|= sscanf(line, "%999[^=]=%999[^\n]\n", tmp, tmp2) - 2;
        if(!e){
          opt_default(tmp, tmp2);
        }
      }

      fclose(f);

    }
    /*
    printf("%d\n", stream_->codec->me_range);
    printf("%d\n", stream_->codec->max_qdiff);
    printf("%d\n", stream_->codec->qmin);
    printf("%d\n", stream_->codec->qmax);
    printf("%f\n", stream_->codec->qcompress);
    */
    //stream_->codec->me_range = me_range_;
    //stream_->codec->max_qdiff = max_qdiff_;
    //stream_->codec->qmin = qmin_;
    //stream_->codec->qmax = qmax_;
    //stream_->codec->qcompress = qcompress_;
    /*
    printf("%d\n", stream_->codec->me_range);
    printf("%d\n", stream_->codec->max_qdiff);
    printf("%d\n", stream_->codec->qmin);
    printf("%d\n", stream_->codec->qmax);
    printf("%f\n", stream_->codec->qcompress);
    */

    stream_->codec->mb_decision = mbDecision_;

    PixelFormat pixFMT = PIX_FMT_YUV420P;;

    if( videoCodec->pix_fmts != NULL ) {
      pixFMT = *videoCodec->pix_fmts;
    }
    else {
      if( strcmp( fmt->name, "gif" ) == 0 ){
        pixFMT = PIX_FMT_RGB24;
      }

    }

    if( strcmp (codecsShortNames_[codec_], "rawvideo") == 0) {
      pixFMT = PIX_FMT_UYVY422;
    }

    stream_->codec->pix_fmt = pixFMT;   // this is set to the first element of FMT a choice could be added
    
    if (!strcmp(avformatOptions_->oformat->name, "mp4") || !strcmp(avformatOptions_->oformat->name, "mov") || !strcmp(avformatOptions_->oformat->name, "3gp"))
      stream_->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;

    if (av_set_parameters(avformatOptions_, NULL) < 0) {
      iop->error("unable to set parameters");
      freeFormat();
      return;
    }

    dump_format(avformatOptions_, 0, filename(), 1);

    if (avcodec_open(stream_->codec, videoCodec) < 0) {
      iop->error("unable to open codec");
      freeFormat();
      return;
    }

    if (!(fmt->flags & AVFMT_NOFILE)) {
      if (url_fopen(&avformatOptions_->pb, filename(), URL_WRONLY) < 0) {
        iop->error("unable to open file");
        return;
      }
    }

    av_write_header(avformatOptions_);
  }

  error_ = CLEANUP;

  AVPicture picture;
  int picSize = avpicture_get_size(PIX_FMT_RGB24, width(), height());
  uint8_t* buffer = (uint8_t*) av_malloc(picSize);
  avpicture_fill(&picture, buffer, PIX_FMT_RGB24, width(), height());

  Row row(0, width());
  input0().validate();
  input0().request(0, 0, width(), height(), Mask_RGB, 1);

  for (int y = 0; y < height(); ++y) {
    get(y, 0, width(), Mask_RGB, row);
    if (iop->aborted())
      return;

    for (Channel z = Chan_Red; z <= Chan_Blue; incr(z)) {
      const float* from = row[z];
      to_byte(z - 1, picture.data[0] + (height() - y - 1) * picture.linesize[0] + z - 1, from, NULL, width(), 3);
    }
  }

  AVFrame* output = avcodec_alloc_frame();
  avcodec_get_frame_defaults(output);
  picSize = avpicture_get_size(stream_->codec->pix_fmt, width(), height());
  uint8_t* outBuffer = (uint8_t*) av_malloc(picSize);
  avpicture_fill((AVPicture*)output, outBuffer, stream_->codec->pix_fmt, width(), height());

  static struct SwsContext *img_convert_ctx;

  img_convert_ctx = sws_getContext(width(), height(), PIX_FMT_RGB24, width(), height(), stream_->codec->pix_fmt, SWS_BICUBIC, NULL, NULL, NULL);
  
  if(img_convert_ctx == NULL) {
    iop->error("Cannot initialize the conversion context!\n");
    return;
  }  
  
  sws_scale(img_convert_ctx, picture.data, picture.linesize, 0, height(), output->data, output->linesize);
  
  // img_convert((AVPicture*) output, stream_->codec->pix_fmt, &picture, PIX_FMT_RGB24, width(), height());

  int ret = 0;
  if ((avformatOptions_->oformat->flags & AVFMT_RAWPICTURE) != 0) {
    AVPacket pkt;
    av_init_packet(&pkt);
    pkt.flags |= PKT_FLAG_KEY;
    pkt.stream_index = stream_->index;
    pkt.data = (uint8_t*) output;
    pkt.size = sizeof(AVPicture);
    ret = av_interleaved_write_frame(avformatOptions_, &pkt);
  }
  else {
    uint8_t* outbuf = (uint8_t*) av_malloc(picSize);
    ret = avcodec_encode_video(stream_->codec, outbuf, picSize, output);
    if (ret > 0) {
      AVPacket pkt;
      av_init_packet(&pkt);
      if (stream_->codec->coded_frame && static_cast<unsigned long>(stream_->codec->coded_frame->pts) != AV_NOPTS_VALUE)
        pkt.pts = av_rescale_q(stream_->codec->coded_frame->pts, stream_->codec->time_base, stream_->time_base);
      if (stream_->codec->coded_frame && stream_->codec->coded_frame->key_frame)
        pkt.flags |= PKT_FLAG_KEY;

      pkt.stream_index = stream_->index;
      pkt.data = outbuf;
      pkt.size = ret;
      ret = av_interleaved_write_frame(avformatOptions_, &pkt);
    }

    av_free(outbuf);
  }

  av_free(outBuffer);
  av_free(buffer);
  av_free(output);

  if (ret) {
    iop->error("error writing frame to file");
    return;
  }

  error_ = SUCCESS;
}

void ffmpegWriter::finish()
{
  if (error_ == IGNORE_FINISH)
    return;
  av_write_trailer(avformatOptions_);
  avcodec_close(stream_->codec);
  if (!(avformatOptions_->oformat->flags & AVFMT_NOFILE))
    url_fclose(avformatOptions_->pb);
  freeFormat();
}

void ffmpegWriter::knobs(Knob_Callback f)
{
  static std::vector<const char*> formatsAliases;

  formatsAliases.resize(formatsLongNames_.size());
  for (int i = 0; i < static_cast<int>(formatsLongNames_.size()); ++i)
    formatsAliases[i] = formatsLongNames_[i].c_str();
  formatsAliases.push_back(0);

  Enumeration_knob(f, &format_, &formatsAliases[0], "format");
  Float_knob(f, &fps_, IRange(0.0, 100.0f), "fps");

  BeginClosedGroup(f, "Advanced");

  Enumeration_knob(f, &codec_, &codecsLongNames_[0], "codec");
  Int_knob(f, &bitrate_, IRange(0.0, 400000), "bitrate");
  SetFlags(f, Knob::SLIDER | Knob::LOG_SLIDER);
  Int_knob(f, &bitrateTolerance_, IRange(0, 4000 * 10000), "bitrateTol", "bitrate tolerance");
  SetFlags(f, Knob::SLIDER | Knob::LOG_SLIDER);

  static std::vector<const char*> ffPresetAliases;
  ffPresetAliases.resize(ffmpegPresets.size());
  for (int i = 0; i < static_cast<int>(ffmpegPresets.size()); ++i) {
    ffPresetAliases[i] = ffmpegPresets[i].c_str();
  }
  ffPresetAliases.push_back(0);

  Enumeration_knob(f, &ffPreset, &ffPresetAliases[0], "ffpreset");

  static const char* mbDecisionTypes[] = {
    "FF_MB_DECISION_SIMPLE", "FF_MB_DECISION_BITS", "FF_MB_DECISION_RD", 0
  };

  Enumeration_knob(f, &mbDecision_, mbDecisionTypes, "mbDecision", "macro block decision mode");

  EndGroup(f);
}

void ffmpegWriter::freeFormat()
{
  for (int i = 0; i < static_cast<int>(avformatOptions_->nb_streams); ++i)
    av_freep(&avformatOptions_->streams[i]);
  av_free(avformatOptions_);
  avformatOptions_ = 0;
  stream_ = 0;
}

static Writer* build(Write* iop)
{
  return new ffmpegWriter(iop);
}

const Writer::Description ffmpegWriter::d("ffmpeg\0mov\0avi\0", build);
