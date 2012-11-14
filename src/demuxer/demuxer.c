/*
 *      vdr-plugin-xvdr - XVDR server plugin for VDR
 *
 *      Copyright (C) 2010 Alwin Esch (Team XBMC)
 *      Copyright (C) 2010 Alexander Pipelka
 *
 *      https://github.com/pipelka/vdr-plugin-xvdr
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include <stdlib.h>
#include <assert.h>
#include <vdr/remux.h>

#include "config/config.h"
#include "live/livestreamer.h"
#include "demuxer.h"
#include "parser.h"
#include "demuxer_LATM.h"
#include "demuxer_AC3.h"
#include "demuxer_EAC3.h"
#include "demuxer_h264.h"
#include "demuxer_MPEGAudio.h"
#include "demuxer_MPEGVideo.h"
#include "demuxer_Subtitle.h"

#define DVD_TIME_BASE 1000000

cTSDemuxer::cTSDemuxer(cLiveStreamer *streamer, eStreamType type, int pid)
  : m_Streamer(streamer)
  , m_streamType(type)
  , m_PID(pid)
  , m_parsed(false)
  , m_audiotype(0)
{
  m_pesParser       = NULL;
  m_language[0]     = 0;
  m_FpsScale        = 0;
  m_FpsRate         = 0;
  m_Height          = 0;
  m_Width           = 0;
  m_Aspect          = 0.0f;
  m_Channels        = 0;
  m_SampleRate      = 0;
  m_BitRate         = 0;
  m_BitsPerSample   = 0;
  m_BlockAlign      = 0;

  switch (m_streamType)
  {
    case stMPEG2VIDEO:
      m_pesParser = new cParserMPEG2Video(this);
      m_streamContent = scVIDEO;
      break;

    case stH264:
      m_pesParser = new cParserH264(this);
      m_streamContent = scVIDEO;
      break;

    case stMPEG2AUDIO:
      m_pesParser = new cParserMPEG2Audio(this);
      m_streamContent = scAUDIO;
      break;

    case stAAC:
      m_pesParser = new cParser(this);
      m_streamContent = scAUDIO;
      break;

    case stLATM:
      m_pesParser = new cParserLATM(this);
      m_streamContent = scAUDIO;
      break;

    case stAC3:
      m_pesParser = new cParserAC3(this);
      m_streamContent = scAUDIO;
      break;

    case stDTS:
      m_pesParser = new cParser(this);
      m_streamContent = scAUDIO;
      break;

    case stEAC3:
      m_pesParser = new cParserEAC3(this);
      m_streamContent = scAUDIO;
      break;

    case stTELETEXT:
      m_pesParser = new cParser(this);
      m_parsed = true;
      m_streamContent = scTELETEXT;
      break;

    case stDVBSUB:
      m_pesParser = new cParserSubtitle(this);
      m_parsed = true;
      m_streamContent = scSUBTITLE;
      break;

    default:
      ERRORLOG("Unrecognised type %i", m_streamType);
      m_streamContent = scNONE;
      break;
  }
}

cTSDemuxer::~cTSDemuxer()
{
  delete m_pesParser;
}

int64_t cTSDemuxer::Rescale(int64_t a)
{
  uint64_t b = DVD_TIME_BASE;
  uint64_t c = 90000;
  uint64_t r = c/2;

  if (b<=INT_MAX && c<=INT_MAX){
    if (a<=INT_MAX)
      return (a * b + r)/c;
    else
      return a/c*b + (a%c*b + r)/c;
  }
  else
  {
    uint64_t a0= a&0xFFFFFFFF;
    uint64_t a1= a>>32;
    uint64_t b0= b&0xFFFFFFFF;
    uint64_t b1= b>>32;
    uint64_t t1= a0*b1 + a1*b0;
    uint64_t t1a= t1<<32;

    a0 = a0*b0 + t1a;
    a1 = a1*b1 + (t1>>32) + (a0<t1a);
    a0 += r;
    a1 += a0<r;

    for (int i=63; i>=0; i--)
    {
      a1+= a1 + ((a0>>i)&1);
      t1+=t1;
      if (c <= a1)
      {
        a1 -= c;
        t1++;
      }
    }
    return t1;
  }
}

void cTSDemuxer::SendPacket(sStreamPacket *pkt)
{
  if(pkt->dts == DVD_NOPTS_VALUE) return;
  if(pkt->pts == DVD_NOPTS_VALUE) return;

  int64_t dts = pkt->dts;
  int64_t pts = pkt->pts;

  // Rescale
  pkt->type     = m_streamType;
  pkt->content  = m_streamContent;
  pkt->pid      = GetPID();
  pkt->dts      = Rescale(dts);
  pkt->pts      = Rescale(pts);
  pkt->duration = Rescale(pkt->duration);

  m_Streamer->sendStreamPacket(pkt);
}

bool cTSDemuxer::ProcessTSPacket(unsigned char *data)
{
  if (data == NULL)
    return false;

  bool pusi  = TsPayloadStart(data);
  int  bytes = TS_SIZE - TsPayloadOffset(data);

  if(bytes < 0 || bytes > TS_SIZE)
    return false;

  if (TsError(data))
  {
    ERRORLOG("transport error");
    return false;
  }

  if (!TsHasPayload(data))
  {
    DEBUGLOG("no payload, size %d", bytes);
    return true;
  }

  // strip ts header
  // TODO: remove this when all parsers are rewritten
  data += TS_SIZE - bytes;

  /* Parse the data */
  if (m_pesParser)
    m_pesParser->Parse(data, bytes, pusi);

  return true;
}

void cTSDemuxer::SetLanguageDescriptor(const char *language, uint8_t atype)
{
  m_language[0] = language[0];
  m_language[1] = language[1];
  m_language[2] = language[2];
  m_language[3] = 0;
  m_audiotype = atype;
}

void cTSDemuxer::SetVideoInformation(int FpsScale, int FpsRate, int Height, int Width, float Aspect, int num, int den)
{
  // check for sane picture information
  if(Width < 320 || Height < 240 || num <= 0 || den <= 0 || Aspect < 0)
    return;

  // only register changed video information
  if(Width == m_Width && Height == m_Height && Aspect == m_Aspect && m_Streamer->IsReady())
    return;

  INFOLOG("--------------------------------------");
  INFOLOG("NEW PICTURE INFORMATION:");
  INFOLOG("Picture Width: %i", Width);
  INFOLOG("Picture Height: %i", Height);

  if(num != 1 || den != 1)
    INFOLOG("Pixel Aspect: %i:%i", num, den);

  if(Aspect == 0)
    INFOLOG("Unknown Display Aspect Ratio");
  else 
    INFOLOG("Display Aspect Ratio: %.2f", Aspect);

  INFOLOG("--------------------------------------");

  m_FpsScale = FpsScale;
  m_FpsRate  = FpsRate;
  m_Height   = Height;
  m_Width    = Width;
  m_Aspect   = Aspect;
  m_parsed   = true;

  if(m_Streamer->IsReady())
    m_Streamer->RequestStreamChange();
}

void cTSDemuxer::SetAudioInformation(int Channels, int SampleRate, int BitRate, int BitsPerSample, int BlockAlign)
{
  // only register changed audio information
  if(Channels == m_Channels && SampleRate == m_SampleRate && BitRate == m_BitRate)
    return;

  INFOLOG("--------------------------------------");
  INFOLOG("NEW AUDIO INFORMATION:");
  INFOLOG("Channels: %i", Channels);
  INFOLOG("Samplerate: %i Hz", SampleRate);
  if(BitRate > 0)
    INFOLOG("Bitrate: %i bps", BitRate);
  INFOLOG("--------------------------------------");

  m_Channels      = Channels;
  m_SampleRate    = SampleRate;
  m_BlockAlign    = BlockAlign;
  m_BitRate       = BitRate;
  m_BitsPerSample = BitsPerSample;
  m_parsed        = true;
}

void cTSDemuxer::SetSubtitlingDescriptor(unsigned char SubtitlingType, uint16_t CompositionPageId, uint16_t AncillaryPageId)
{
  m_subtitlingType    = SubtitlingType;
  m_compositionPageId = CompositionPageId;
  m_ancillaryPageId   = AncillaryPageId;
  m_parsed            = true;
}
