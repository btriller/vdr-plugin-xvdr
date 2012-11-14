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

#include "demuxer_EAC3.h"
#include "bitstream.h"
#include "ac3common.h"

const uint8_t EAC3Blocks[4] = {
  1, 2, 3, 6
};

typedef enum {
  EAC3_FRAME_TYPE_INDEPENDENT = 0,
  EAC3_FRAME_TYPE_DEPENDENT,
  EAC3_FRAME_TYPE_AC3_CONVERT,
  EAC3_FRAME_TYPE_RESERVED
} EAC3FrameType;

cParserEAC3::cParserEAC3(cTSDemuxer *demuxer) : cParser(demuxer)
{
  m_headersize = AC3_HEADER_SIZE;
  m_disablealignment = false;
}

bool cParserEAC3::CheckAlignmentHeader(unsigned char* buffer, int& framesize) {
  if(!(buffer[0] == 0x0b && buffer[1] == 0x77))
    return false;

  cBitstream bs(buffer, 40);
  bs.skipBits(16); // Syncword
  bs.readBits(2);  // frametype
  bs.readBits(3);  // substream id

  framesize = (bs.readBits(11) + 1) << 1;
}

void cParserEAC3::ParsePayload(unsigned char* payload, int length) {
  uint32_t header = ((payload[0] << 24) | (payload[1] << 16) | (payload[2] <<  8) | payload[3]);

  if (!(payload[0] == 0x0b && payload[1] == 0x77))
    return;

  int SampleRate = 0;
  int BitRate = 0;
  int Channels = 0;

  cBitstream bs(payload + 2, AC3_HEADER_SIZE * 8);

  /* read ahead to bsid to distinguish between AC-3 and EAC-3 */
  int bsid = bs.showBits(29) & 0x1F;
  if (bsid < 10 || bsid > 16)
    return;

  /* Enhanced AC-3 */
  int frametype = bs.readBits(2);
  if (frametype == EAC3_FRAME_TYPE_RESERVED)
    return;

  bs.readBits(3);

  int FrameSize = (bs.readBits(11) + 1) << 1;
  if (FrameSize < AC3_HEADER_SIZE)
   return;

  int numBlocks = 6;
  int sr_code = bs.readBits(2);
  if (sr_code == 3)
  {
    int sr_code2 = bs.readBits(2);
    if (sr_code2 == 3)
      return;

    SampleRate = AC3SampleRateTable[sr_code2] / 2;
  }
  else
  {
    numBlocks = EAC3Blocks[bs.readBits(2)];
    SampleRate = AC3SampleRateTable[sr_code];
  }

  int channelMode = bs.readBits(3);
  int lfeon = bs.readBits(1);

  BitRate  = (uint32_t)(8.0 * FrameSize * SampleRate / (numBlocks * 256.0));
  Channels = AC3ChannelsTable[channelMode] + lfeon;

  m_demuxer->SetAudioInformation(Channels, SampleRate, BitRate, 0, 0);
}
