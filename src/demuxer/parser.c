/*
 *      vdr-plugin-xvdr - XBMC server plugin for VDR
 *
 *      Copyright (C) 2012 Alexander Pipelka
 *
 *      http://www.xbmc.org
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

#include "parser.h"
#include "config/config.h"
#include "bitstream.h"
#include "pes.h"

cParser::cParser(cTSDemuxer *demuxer) : cTsToPes(), m_demuxer(demuxer)
{
  m_packet = NULL;
  m_packetsize = 0;
  m_alignbuffer = NULL;
  m_alignbuffersize = 0;

  m_headersize = 0;
  m_disablealignment = true;

  m_curPTS = DVD_NOPTS_VALUE;
  m_curDTS = DVD_NOPTS_VALUE;
  m_PTS = DVD_NOPTS_VALUE;
  m_DTS = DVD_NOPTS_VALUE;
}

cParser::~cParser()
{
  free(m_packet);
  free(m_alignbuffer);
}

int64_t cParser::PesGetPTS(const uint8_t *buf, int len)
{
  /* assume mpeg2 pes header ... */
  if (PesIsVideoPacket(buf) || PesIsAudioPacket(buf)) {

    if ((buf[6] & 0xC0) != 0x80)
      return DVD_NOPTS_VALUE;
    if ((buf[6] & 0x30) != 0)
      return DVD_NOPTS_VALUE;

    if ((len > 13) && (buf[7] & 0x80)) { /* pts avail */
      int64_t pts;
      pts  = ((int64_t)(buf[ 9] & 0x0E)) << 29 ;
      pts |= ((int64_t) buf[10])         << 22 ;
      pts |= ((int64_t)(buf[11] & 0xFE)) << 14 ;
      pts |= ((int64_t) buf[12])         <<  7 ;
      pts |= ((int64_t)(buf[13] & 0xFE)) >>  1 ;
      return pts;
    }
  }
  return DVD_NOPTS_VALUE;
}

int64_t cParser::PesGetDTS(const uint8_t *buf, int len)
{
  if (PesIsVideoPacket(buf) || PesIsAudioPacket(buf))
  {
    if ((buf[6] & 0xC0) != 0x80)
      return DVD_NOPTS_VALUE;
    if ((buf[6] & 0x30) != 0)
      return DVD_NOPTS_VALUE;

    if (len > 18 && (buf[7] & 0x40)) { /* dts avail */
      int64_t dts;
      dts  = ((int64_t)( buf[14] & 0x0E)) << 29 ;
      dts |=  (int64_t)( buf[15]         << 22 );
      dts |=  (int64_t)((buf[16] & 0xFE) << 14 );
      dts |=  (int64_t)( buf[17]         <<  7 );
      dts |=  (int64_t)((buf[18] & 0xFE) >>  1 );
      return dts;
    }
  }
  return DVD_NOPTS_VALUE;
}

int cParser::ParsePESHeader(uint8_t *buf, size_t len)
{
  // parse PES header
  unsigned int hdr_len = PesPayloadOffset(buf);

  // PTS / DTS
  int64_t pts = PesGetPTS(buf, len);
  int64_t dts = PesGetDTS(buf, len);

  if (dts == DVD_NOPTS_VALUE)
   dts = pts;

  dts = dts & PTS_MASK;
  pts = pts & PTS_MASK;

  if(pts != 0) m_curDTS = dts;
  if(dts != 0) m_curPTS = pts;

  if (m_DTS == DVD_NOPTS_VALUE)
    m_DTS = m_curDTS;

  if (m_PTS == DVD_NOPTS_VALUE)
    m_PTS = m_curPTS;

  return hdr_len;
}

bool cParser::AllocatePacket(int length, uint8_t** buffer, int* buffersize) {
  if(*buffer == NULL) {
    *buffer = (uint8_t*)malloc(length);

    if(*buffer == NULL)
    {
      ERRORLOG("PARSER: Unable to allocate packet memory!");
      return false;
    }

    *buffersize = length;
    return true;
  }

  if (length <= *buffersize) {
    return true;
  }

  uint8_t* packet = (uint8_t*)realloc(*buffer, length);
  if(packet == NULL)
  {
    ERRORLOG("PARSER: Unable to resize packet !");
    return false;
  }

  *buffer = packet;
  *buffersize = length;

  return true;
}

void cParser::SendPayload(unsigned char* payload, int length)
{
  sStreamPacket pkt;
  pkt.data     = payload;
  pkt.size     = length;
  pkt.duration = 0;
  pkt.dts      = m_DTS;
  pkt.pts      = m_PTS;

  m_demuxer->SendPacket(&pkt);

  m_DTS = DVD_NOPTS_VALUE;
  m_PTS = DVD_NOPTS_VALUE;
}

void cParser::Parse(unsigned char *data, int size, bool pusi)
{
  int length = 0;
  int total_length = 0;

  // -------------
  // DEMUX
  // -------------

  // for backward compatibility (this will change in future)
  data -= (TS_SIZE - size);

  // check if packet is complete
  const uchar* pes = NULL;

  if(pusi)
    pes = GetPes(length);

  // process data
  if(pes == NULL)
  {
    PutTs(data, TS_SIZE);
    return;
  }

  // create or resize packet buffer
  if(!AllocatePacket(length, &m_packet, &m_packetsize))
  {
    Reset();
    return;
  }

  // copy data
  memcpy(m_packet, pes, length);
  total_length = length;

  // more to come ?
  while((pes = GetPes(length)) != NULL) {

    int offset = ParsePESHeader((uint8_t*)pes, length);
    length -= offset;

    // increase packet size
    if(!AllocatePacket(total_length + length, &m_packet, &m_packetsize))
    {
      Reset();
      return;
    }

    // add data to packet
    memcpy(m_packet + total_length, pes + offset, length);
    total_length += length;
  }

  // assembly of payload completed
  int offset = ParsePESHeader(m_packet, total_length);

  uint8_t* payload = m_packet + offset;
  int payloadlength = total_length - offset;

  // -------------
  // ALIGN PAYLOAD
  // -------------

  // data alignment indicator
  bool dai = m_packet[6] & 0x04;

  // align payload
  if(!dai && !m_disablealignment)
  {
    DEBUGLOG("Aligning stream (pid %i)", m_demuxer->GetPID());
    if (m_alignbuffersize > 256 * 1024)
    {
      ERRORLOG("alignment buffer overrun. resetting.");
      m_alignbuffersize = 0;
    }
    int framesize = 0;

    // copy packet into buffer
    int o = m_alignbuffersize;
    if(!AllocatePacket(o + payloadlength, &m_alignbuffer, &m_alignbuffersize))
    {
      Reset();
      m_alignbuffersize = 0;
      return;
    }
    memcpy(m_alignbuffer + o, payload, payloadlength);

    DEBUGLOG("Alignbuffer: %i bytes", m_alignbuffersize);

    // find alignment
    int startpos = -1;
    payloadlength = 0;
    o = 0;
    while((o = FindAlignmentOffset(m_alignbuffer, m_alignbuffersize, o, framesize)) != -1)
    {
      if(startpos == -1)
        startpos = o;

      DEBUGLOG("Found alignment offset: %i framesize: %i", o, framesize);
      o += framesize;
      payloadlength += framesize;
    }

    if(startpos != -1)
    {
      payload = m_alignbuffer + startpos;
      ParsePayload(payload, payloadlength);

      SendPayload(payload, payloadlength);

      // shift alignment buffer
      int remainder = m_alignbuffersize - (startpos + payloadlength);

      if(remainder > 0)
      {
        DEBUGLOG("shifting alignment buffer (remainder = %i)", remainder);
        memmove(m_alignbuffer, payload + payloadlength, remainder);
        m_alignbuffersize = remainder;
      }
      else
        m_alignbuffersize = 0;

      DEBUGLOG("buffersize after shift: %i bytes", m_alignbuffersize);
    }
  }
  else
  {
    // parse payload
    ParsePayload(payload, payloadlength);

    // send payload
    SendPayload(payload, payloadlength);
  }

  // put new data into queue
  PutTs(data, TS_SIZE);
}

void cParser::ParsePayload(unsigned char* payload, int length)
{
}

int cParser::FindAlignmentOffset(unsigned char* buffer, int buffersize, int startoffset, int& framesize) {
  framesize = 0;

  if(m_disablealignment)
    return -1;

  int o = startoffset;
  int tmp = 0;

  // seek syncword
  while(o < (buffersize - m_headersize) && !CheckAlignmentHeader(buffer + o, framesize))
    o++;

  // not found
  if(o >= buffersize - m_headersize)
    return -1;

  // buffer already aligned ?
  if(o == 0 && framesize == buffersize)
    return 0;

  // no framesize -> check next sync word
  if(framesize == 0)
  {
    int p = FindAlignmentOffset(buffer, buffersize, o + 1, tmp);

    if(p != -1)
      return -1;

    framesize = p - o;
  }

  // buffer too small for complete frame ?
  if(buffersize - m_headersize <= o + framesize)
    return -1;

  // next alignment header found ?
  if(!CheckAlignmentHeader(&buffer[o + framesize], tmp))
    return FindAlignmentOffset(buffer, buffersize, ++o, tmp);

  return o;
}

bool cParser::CheckAlignmentHeader(unsigned char* buffer, int& framesize) {
  framesize = 0;
  return true;
}
