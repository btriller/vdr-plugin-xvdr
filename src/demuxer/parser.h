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

#ifndef XVDR_DEMUXER_BASE_H
#define XVDR_DEMUXER_BASE_H

#include "demuxer.h"
#include "vdr/remux.h"

class cParser : private cTsToPes
{
public:

  cParser(cTSDemuxer *demuxer);

  virtual ~cParser();

  virtual void Parse(unsigned char *data, int size, bool pusi);

protected:

  int ParsePESHeader(uint8_t *buf, size_t len);

  virtual void ParsePayload(unsigned char* payload, int length);

  virtual bool CheckAlignmentHeader(unsigned char* buffer, int& framesize);

  cTSDemuxer* m_demuxer;

  int64_t m_curPTS;
  int64_t m_curDTS;
  int64_t m_PTS;
  int64_t m_DTS;

  int m_headersize;

  int m_disablealignment;

private:

  int64_t PesGetPTS(const uint8_t *buf, int len);

  int64_t PesGetDTS(const uint8_t *buf, int len);

  int FindAlignmentOffset(unsigned char* buffer, int buffersize, int startoffset, int& framesize);

  bool AllocatePacket(int length, uint8_t** buffer, int* buffersize);

  void SendPayload(unsigned char* payload, int length);

  uint8_t* m_packet;

  int m_packetsize;

  uint8_t* m_alignbuffer;

  int m_alignbuffersize;

};

#endif // XVDR_DEMUXER_BASE_H
