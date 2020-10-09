#ifndef MYTHTRANSCODEPLAYER_H
#define MYTHTRANSCODEPLAYER_H

// MythTV
#include "mythplayer.h"

class MTV_PUBLIC MythTranscodePlayer : public MythPlayer
{
  public:
    MythTranscodePlayer(PlayerContext* Context, PlayerFlags Flags = kNoFlags);

    void SetTranscoding        (bool Transcoding);
    void InitForTranscode      (bool CopyAudio, bool CopyVideo);
    void SetCutList            (const frm_dir_map_t& CutList);
    bool TranscodeGetNextFrame (int& DidFF, bool& KeyFrame, bool HonorCutList);
    bool WriteStoredData       (MythMediaBuffer* OutBuffer, bool WriteVideo, long TimecodeOffset);
    long UpdateStoredFrameNum  (long CurrentFrameNum);
};

#endif