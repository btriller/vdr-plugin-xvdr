/*
 *      vdr-plugin-xvdr - XVDR server plugin for VDR
 *
 *      Copyright (C) 2010 Alwin Esch (Team XBMC)
 *      Copyright (C) 2012 Alexander Pipelka
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

#include "demuxer_AC3.h"
#include "bitstream.h"
#include "ac3common.h"

cParserAC3::cParserAC3(cTSDemuxer *demuxer) : cParser(demuxer)
{
  m_headersize = AC3_HEADER_SIZE;
  m_disablealignment = false;
}

bool cParserAC3::CheckAlignmentHeader(unsigned char* buffer, int& framesize) {
  if(!(buffer[0] == 0x0b && buffer[1] == 0x77))
    return false;

  cBitstream bs(buffer, 40);
  bs.skipBits(16);                // Syncword
  bs.skipBits(16);                // CRC1
  int fscod = bs.readBits(2);     // fscod
  int frmsizcod = bs.readBits(6); // frmsizcod

  if (fscod == 3 || frmsizcod > 37)
    return false;

  framesize = AC3FrameSizeTable[frmsizcod][fscod] * 2;
  return true;
}

void cParserAC3::ParsePayload(unsigned char* payload, int length) {
  uint32_t header = ((payload[0] << 24) | (payload[1] << 16) | (payload[2] <<  8) | payload[3]);

  if (!(payload[0] == 0x0b && payload[1] == 0x77))
    return;

  int SampleRate = 0;
  int BitRate = 0;
  int Channels = 0;

  cBitstream bs(payload + 2, AC3_HEADER_SIZE * 8);

  /* read ahead to bsid to distinguish between AC-3 and EAC-3 */
  int bsid = bs.showBits(29) & 0x1F;
  if (bsid > 10)
    return;

  /* Normal AC-3 */
  bs.skipBits(16);
  int fscod       = bs.readBits(2);
  int frmsizecod  = bs.readBits(6);
  bs.skipBits(5); // skip bsid, already got it
  bs.skipBits(3); // skip bitstream mode
  int acmod       = bs.readBits(3);

  if (fscod == 3 || frmsizecod > 37)
    return;

  if (acmod == AC3_CHMODE_STEREO)
  {
    bs.skipBits(2); // skip dsurmod
  }
  else
  {
    if ((acmod & 1) && acmod != AC3_CHMODE_MONO)
      bs.skipBits(2);
    if (acmod & 4)
      bs.skipBits(2);
  }
  int lfeon = bs.readBits(1);

  int srShift = max(bsid, 8) - 8;
  SampleRate  = AC3SampleRateTable[fscod] >> srShift;
  BitRate     = (AC3BitrateTable[frmsizecod>>1] * 1000) >> srShift;
  Channels    = AC3ChannelsTable[acmod] + lfeon;

  m_demuxer->SetAudioInformation(Channels, SampleRate, BitRate, 0, 0);
}
