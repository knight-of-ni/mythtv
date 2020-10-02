// MythTV
#include "mythlogging.h"
#include "mythmainwindow.h"
#include "mythplayer.h"
#include "mythpaintergpu.h"
#include "mythvideogpu.h"
#include "mythvideooutgpu.h"

#define LOC QString("VidOutGPU: ")

/*! \class MythVideoOutputGPU
 * \brief Common code shared between GPU accelerated sub-classes (e.g. OpenGL)
 *
 * MythVideoOutputGPU is a pure virtual class that contains code shared between
 * the differing hardware accelerated MythVideoOutput subclasses.
 *
 * \note This should be considered a work-in-progress and it is likely to change.
 *
 * \sa MythVideoOutput
 * \sa MythVideoOutputOpenGL
 * \sa MythVideoOutputVulkan
 */
MythVideoOutputGPU::MythVideoOutputGPU(MythRender* Render, QString& Profile)
  : MythVideoOutput(true),
    m_render(Render),
    m_profile(std::move(Profile))
{
    if (m_render)
        m_render->IncrRef();

    MythMainWindow* win = MythMainWindow::getMainWindow();
    if (win)
    {
        m_painter = dynamic_cast<MythPainterGPU*>(win->GetPainter());
        if (m_painter)
            m_painter->SetViewControl(MythPainterGPU::None);
    }

    if (!(win && m_render && m_painter))
        LOG(VB_GENERAL, LOG_ERR, LOC + "Fatal error");
}

MythVideoOutputGPU::~MythVideoOutputGPU()
{
    MythVideoOutputGPU::DestroyVisualisation();
    MythVideoOutputGPU::DestroyBuffers();
    delete m_video;
    if (m_painter)
        m_painter->SetViewControl(MythPainterGPU::Viewport | MythPainterGPU::Framebuffer);
    if (m_render)
        m_render->DecrRef();
}

MythPainter* MythVideoOutputGPU::GetOSDPainter()
{
    return m_painter;
}

QRect MythVideoOutputGPU::GetDisplayVisibleRectAdj()
{
    return GetDisplayVisibleRect();
}

void MythVideoOutputGPU::InitPictureAttributes()
{
    m_videoColourSpace.SetSupportedAttributes(ALL_PICTURE_ATTRIBUTES);
}

void MythVideoOutputGPU::WindowResized(const QSize& Size)
{
    SetWindowSize(Size);
    InitDisplayMeasurements();
}

void MythVideoOutputGPU::SetVideoFrameRate(float NewRate)
{
    if (!m_dbDisplayProfile)
        return;

    if (qFuzzyCompare(m_dbDisplayProfile->GetOutput() + 1.0F, NewRate + 1.0F))
        return;

    LOG(VB_PLAYBACK, LOG_INFO, LOC + QString("Video frame rate changed: %1->%2")
        .arg(static_cast<double>(m_dbDisplayProfile->GetOutput())).arg(static_cast<double>(NewRate)));
    m_dbDisplayProfile->SetOutput(NewRate);
    m_newFrameRate = true;
}

bool MythVideoOutputGPU::Init(const QSize& VideoDim, const QSize& VideoDispDim,
                              float Aspect, const QRect& DisplayVisibleRect, MythCodecID CodecId)
{
    // if we are the main video player then free up as much video memory
    // as possible at startup
    if ((kCodec_NONE == m_newCodecId) && m_painter)
        m_painter->FreeResources();

    // Default initialisation - mainly MythVideoBounds
    if (!MythVideoOutput::Init(VideoDim, VideoDispDim, Aspect, DisplayVisibleRect, CodecId))
        return false;

    // Ensure any new profile preferences are handled after a stream change
    if (m_dbDisplayProfile)
        m_video->SetProfile(m_dbDisplayProfile->GetVideoRenderer());

    // Set default support for picture attributes
    InitPictureAttributes();

    // Setup display
    QSize size = GetVideoDim();

    // Set the display mode if required
    if (m_display && m_display->UsingVideoModes() && !IsEmbedding())
        ResizeForVideo(size);
    InitDisplayMeasurements();

    // Create buffers
    if (!CreateBuffers(CodecId, GetVideoDim()))
        return false;

    // Adjust visible rect for embedding
    QRect dvr = GetDisplayVisibleRectAdj();
    if (m_videoCodecID == kCodec_NONE)
    {
        m_render->SetViewPort(QRect(QPoint(), dvr.size()));
        return true;
    }

    // Reset OpenGLVideo
    if (m_video->IsValid())
        m_video->ResetFrameFormat();

    return true;
}

/*! \brief Discard video frames
 *
 * If Flushed is true, the decoder will probably reset the hardware decoder in
 * use and we need to release any hardware pause frames so the decoder is released
 * before a new one is created.
*/
void MythVideoOutputGPU::DiscardFrames(bool KeyFrame, bool Flushed)
{
    if (Flushed)
    {
        LOG(VB_PLAYBACK, LOG_INFO, LOC + QString("(%1): %2").arg(KeyFrame).arg(m_videoBuffers.GetStatus()));
        m_videoBuffers.DiscardPauseFrames();
    }
    MythVideoOutput::DiscardFrames(KeyFrame, Flushed);
}

/*! \brief Release a video frame back into the decoder pool.
 *
 * Software frames do not need a pause frame as the MythVideo subclass
 * holds a copy of the last frame in its input textures. So
 * just release the frame.
 *
 * Hardware frames hold the underlying interop class and
 * hence access to the video texture. We cannot access them
 * without a frame so retain the most recent frame by removing
 * it from the 'used' queue and adding it to the 'pause' queue.
*/
void MythVideoOutputGPU::DoneDisplayingFrame(VideoFrame* Frame)
{
    if (!Frame)
        return;

    bool retain = format_is_hw(Frame->codec);
    QVector<VideoFrame*> release;

    m_videoBuffers.BeginLock(kVideoBuffer_pause);
    while (m_videoBuffers.Size(kVideoBuffer_pause))
    {
        VideoFrame* frame = m_videoBuffers.Dequeue(kVideoBuffer_pause);
        if (!retain || (retain && (frame != Frame)))
            release.append(frame);
    }

    if (retain)
    {
        m_videoBuffers.Enqueue(kVideoBuffer_pause, Frame);
        if (m_videoBuffers.Contains(kVideoBuffer_used, Frame))
            m_videoBuffers.Remove(kVideoBuffer_used, Frame);
    }
    else
    {
        release.append(Frame);
    }
    m_videoBuffers.EndLock();

    for (auto * frame : release)
        m_videoBuffers.DoneDisplayingFrame(frame);
}

bool MythVideoOutputGPU::CreateBuffers(MythCodecID CodecID, QSize Size)
{
    if (m_buffersCreated)
        return true;

    if (codec_is_copyback(CodecID))
    {
        m_videoBuffers.Init(VideoBuffers::GetNumBuffers(FMT_NONE), false, 1, 4, 2);
        return m_videoBuffers.CreateBuffers(FMT_YV12, Size.width(), Size.height());
    }

    if (codec_is_mediacodec(CodecID))
        return m_videoBuffers.CreateBuffers(FMT_MEDIACODEC, Size, false, 1, 2, 2);
    if (codec_is_vaapi(CodecID))
        return m_videoBuffers.CreateBuffers(FMT_VAAPI, Size, false, 2, 1, 4, m_maxReferenceFrames);
    if (codec_is_vtb(CodecID))
        return m_videoBuffers.CreateBuffers(FMT_VTB, Size, false, 1, 4, 2);
    if (codec_is_vdpau(CodecID))
        return m_videoBuffers.CreateBuffers(FMT_VDPAU, Size, false, 2, 1, 4, m_maxReferenceFrames);
    if (codec_is_nvdec(CodecID))
        return m_videoBuffers.CreateBuffers(FMT_NVDEC, Size, false, 2, 1, 4);
    if (codec_is_mmal(CodecID))
        return m_videoBuffers.CreateBuffers(FMT_MMAL, Size, false, 2, 1, 4);
    if (codec_is_v4l2(CodecID) || codec_is_drmprime(CodecID))
        return m_videoBuffers.CreateBuffers(FMT_DRMPRIME, Size, false, 2, 1, 4);

    return m_videoBuffers.CreateBuffers(FMT_YV12, Size, false, 1, 8, 4, m_maxReferenceFrames);
}

void MythVideoOutputGPU::DestroyBuffers()
{
    MythVideoOutputGPU::DiscardFrames(true, true);
    m_videoBuffers.DeleteBuffers();
    m_videoBuffers.Reset();
    m_buffersCreated = false;
}

bool MythVideoOutputGPU::InputChanged(const QSize& VideoDim, const QSize& VideoDispDim,
                                      float Aspect, MythCodecID CodecId, bool& AspectOnly, int ReferenceFrames,
                                      bool ForceChange)
{
    QSize currentvideodim     = GetVideoDim();
    QSize currentvideodispdim = GetVideoDispDim();
    MythCodecID currentcodec  = m_videoCodecID;
    float currentaspect       = GetVideoAspect();

    if (m_newCodecId != kCodec_NONE)
    {
        // InputChanged has been called twice in quick succession without a call to ProcessFrame
        currentvideodim = m_newVideoDim;
        currentvideodispdim = m_newVideoDispDim;
        currentcodec = m_newCodecId;
        currentaspect = m_newAspect;
    }

    LOG(VB_PLAYBACK, LOG_INFO, LOC + QString("Video changed: %1x%2 (%3x%4) '%5' (Aspect %6 Refs %13)"
                                             "-> %7x%8 (%9x%10) '%11' (Aspect %12 Refs %14)")
        .arg(currentvideodispdim.width()).arg(currentvideodispdim.height())
        .arg(currentvideodim.width()).arg(currentvideodim.height())
        .arg(toString(currentcodec)).arg(static_cast<double>(currentaspect))
        .arg(VideoDispDim.width()).arg(VideoDispDim.height())
        .arg(VideoDim.width()).arg(VideoDim.height())
        .arg(toString(CodecId)).arg(static_cast<double>(Aspect))
        .arg(m_maxReferenceFrames).arg(ReferenceFrames));

    bool cidchanged = (CodecId != currentcodec);
    bool reschanged = (VideoDispDim != currentvideodispdim);
    bool refschanged = m_maxReferenceFrames != ReferenceFrames;

    // aspect ratio changes are a no-op as changes are handled at display time
    if (!(cidchanged || reschanged || refschanged || ForceChange))
    {
        AspectOnly = true;
        return true;
    }

    // N.B. We no longer check for interop support for the new codec as it is a
    // poor substitute for a full check of decoder capabilities etc. Better to let
    // hardware decoding fail if necessary - which should at least fallback to
    // software decoding rather than bailing out here.

    // delete and recreate the buffers and flag that the input has changed
    m_maxReferenceFrames = ReferenceFrames;
    m_buffersCreated = m_videoBuffers.DiscardAndRecreate(CodecId, VideoDim, m_maxReferenceFrames);
    if (!m_buffersCreated)
        return false;

    m_newCodecId= CodecId;
    m_newVideoDim = VideoDim;
    m_newVideoDispDim = VideoDispDim;
    m_newAspect = Aspect;
    return true;
}

bool MythVideoOutputGPU::ProcessInputChange()
{
    if (m_newCodecId != kCodec_NONE)
    {
        // Ensure we don't lose embedding through program changes.
        bool wasembedding = IsEmbedding();
        QRect oldrect;
        if (wasembedding)
        {
            oldrect = GetEmbeddingRect();
            StopEmbedding();
        }

        // Note - we don't call the default VideoOutput::InputChanged method as
        // the OpenGL implementation is asynchronous.
        // So we need to update the video display profile here. It is a little
        // circular as we need to set the video dimensions first which are then
        // reset in Init.
        // All told needs a cleanup - not least because the use of codecName appears
        // to be inconsistent.
        SourceChanged(m_newVideoDim, m_newVideoDispDim, m_newAspect);
        AVCodecID avCodecId = myth2av_codecid(m_newCodecId);
        AVCodec* codec = avcodec_find_decoder(avCodecId);
        QString codecName;
        if (codec)
            codecName = codec->name;
        if (m_dbDisplayProfile)
            m_dbDisplayProfile->SetInput(GetVideoDispDim(), 0 , codecName);
        bool ok = Init(m_newVideoDim, m_newVideoDispDim, m_newAspect, GetRawWindowRect(), m_newCodecId);
        m_newCodecId = kCodec_NONE;
        m_newVideoDim = QSize();
        m_newVideoDispDim = QSize();
        m_newAspect = 0.0F;
        m_newFrameRate = false;

        if (wasembedding && ok)
            EmbedInWidget(oldrect);

        // Update deinterlacers for any input change
        SetDeinterlacing(m_deinterlacing, m_deinterlacing2X, m_forcedDeinterlacer);

        if (!ok)
            return false;
    }
    else if (m_newFrameRate)
    {
        // If we are switching mode purely for a refresh rate change, then there
        // is no need to recreate buffers etc etc
        ResizeForVideo();
        m_newFrameRate = false;
    }

    return true;
}

/*! \brief Initialise display measurement
 *
 * The sole intent here is to ensure that MythVideoBounds has the correct aspect
 * ratio when it calculates the video display rectangle.
*/
void MythVideoOutputGPU::InitDisplayMeasurements()
{
    if (!m_display)
        return;

    // Retrieve the display aspect ratio.
    // This will be, in priority order:-
    // - aspect ratio override when using resolution/mode switching (if not 'Default')
    // - aspect ratio override for setups where detection does not work/is broken (multiscreen, broken EDID etc)
    // - aspect ratio based on detected physical size (this should be the common/default value)
    // - aspect ratio fallback using screen resolution
    // - 16:9
    QString source;
    double displayaspect = m_display->GetAspectRatio(source);
    LOG(VB_PLAYBACK, LOG_INFO, LOC + QString("Display aspect ratio: %1 (%2)")
        .arg(displayaspect).arg(source));

    // Get the window and screen resolutions
    QSize window = GetRawWindowRect().size();
    QSize screen = m_display->GetResolution();

    // If not running fullscreen, adjust for window size and ignore any video
    // mode overrides as they do not apply when in a window
    if (!window.isEmpty() && !screen.isEmpty() && window != screen)
    {
        displayaspect = m_display->GetAspectRatio(source, true);
        double screenaspect = screen.width() / static_cast<double>(screen.height());
        double windowaspect = window.width() / static_cast<double>(window.height());
        displayaspect = displayaspect * (1.0 / screenaspect) * windowaspect;
        LOG(VB_PLAYBACK, LOG_INFO, LOC + QString("Window aspect ratio: %1").arg(displayaspect));
    }

    SetDisplayAspect(static_cast<float>(displayaspect));
}

void MythVideoOutputGPU::PrepareFrame(VideoFrame* Frame, FrameScanType Scan)
{
    // Process input changes
    if (!ProcessInputChange())
        return;

    if (Frame)
    {
        SetRotation(Frame->rotation);
        if (format_is_hw(Frame->codec) || Frame->dummy)
            return;

        // software deinterlacing
        m_deinterlacer.Filter(Frame, Scan, m_dbDisplayProfile);

        // update software textures
        if (m_video)
            m_video->PrepareFrame(Frame, Scan);
    }
}

void MythVideoOutputGPU::RenderFrame(VideoFrame *Frame, FrameScanType Scan, OSD *Osd)
{
    bool dummy = false;
    bool topfieldfirst = false;
    if (Frame)
    {
        m_framesPlayed = Frame->frameNumber + 1;
        topfieldfirst = Frame->interlaced_reversed ? !Frame->top_field_first : Frame->top_field_first;
        dummy = Frame->dummy;
    }
    else
    {
        // see DoneDisplayingFrame
        // we only retain pause frames for hardware formats
        if (m_videoBuffers.Size(kVideoBuffer_pause))
            Frame = m_videoBuffers.Tail(kVideoBuffer_pause);
    }

    // Main UI when embedded
    if (m_painter && IsEmbedding())
    {
        // If we are using high dpi, the painter needs to set the appropriate
        // viewport and enable scaling of its images
        m_painter->SetViewControl(MythPainterGPU::Viewport);

        MythMainWindow* win = GetMythMainWindow();
        if (win && win->GetPaintWindow())
        {
            win->GetPaintWindow()->clearMask();
            win->Draw(m_painter);
        }
        m_painter->SetViewControl(MythPainterGPU::None);
    }

    // Video
    // N.B. dummy streams need the viewport updated in case we have resized the window (i.e. LiveTV)
    if (m_video && !dummy)
        m_video->RenderFrame(Frame, topfieldfirst, Scan, GetStereoscopicMode());
    else if (dummy)
        m_render->SetViewPort(GetWindowRect());

    const QRect osdbounds = GetTotalOSDBounds();

    // Visualisation
    if (m_visual && m_painter && !IsEmbeddingAndHidden())
        m_visual->Draw(osdbounds, m_painter, nullptr);

    // OSD
    if (Osd && m_painter && !IsEmbedding())
        Osd->Draw(m_painter, osdbounds.size(), true);
}

void MythVideoOutputGPU::UpdatePauseFrame(int64_t& DisplayTimecode, FrameScanType Scan)
{
    VideoFrame* release = nullptr;
    m_videoBuffers.BeginLock(kVideoBuffer_used);
    VideoFrame* used = m_videoBuffers.Head(kVideoBuffer_used);
    if (used)
    {
        if (format_is_hw(used->codec))
        {
            release = m_videoBuffers.Dequeue(kVideoBuffer_used);
        }
        else
        {
            Scan = (is_interlaced(Scan) && !used->already_deinterlaced) ? kScan_Interlaced : kScan_Progressive;
            m_deinterlacer.Filter(used, Scan, m_dbDisplayProfile, true);
            if (m_video)
                m_video->PrepareFrame(used, Scan);
        }
        DisplayTimecode = used->disp_timecode;
    }
    else
    {
        LOG(VB_PLAYBACK, LOG_WARNING, LOC + "Could not update pause frame");
    }
    m_videoBuffers.EndLock();

    if (release)
        DoneDisplayingFrame(release);
}

void MythVideoOutputGPU::EndFrame()
{
    m_video->EndFrame();
}

void MythVideoOutputGPU::ClearAfterSeek()
{
    // Clear reference frames for GPU deinterlacing
    if (m_video)
        m_video->ResetTextures();
    // Clear decoded frames
    MythVideoOutput::ClearAfterSeek();
}

bool MythVideoOutputGPU::EnableVisualisation(AudioPlayer* Audio, bool Enable, const QString& Name)
{
    if (!Enable)
    {
        DestroyVisualisation();
        return false;
    }
    return SetupVisualisation(Audio, Name);
}

QString MythVideoOutputGPU::GetVisualiserName()
{
    if (m_visual)
        return m_visual->Name();
    return MythVideoOutput::GetVisualiserName();
}

void MythVideoOutputGPU::DestroyVisualisation()
{
    delete m_visual;
    m_visual = nullptr;
}

bool MythVideoOutputGPU::StereoscopicModesAllowed() const
{
    return true;
}

QStringList MythVideoOutputGPU::GetVisualiserList()
{
    if (m_render)
        return VideoVisual::GetVisualiserList(m_render->Type());
    return MythVideoOutput::GetVisualiserList();
}

bool MythVideoOutputGPU::CanVisualise(AudioPlayer* Audio)
{
    return VideoVisual::CanVisualise(Audio, m_render);
}

bool MythVideoOutputGPU::SetupVisualisation(AudioPlayer* Audio, const QString& Name)
{
    DestroyVisualisation();
    m_visual = VideoVisual::Create(Name, Audio, m_render);
    return m_visual != nullptr;
}

VideoVisual* MythVideoOutputGPU::GetVisualisation()
{
    return m_visual;
}

/**
 * \fn VideoOutput::ResizeForVideo(uint width, uint height)
 * Sets display parameters based on video resolution.
 *
 * If we are using DisplayRes support we use the video size to
 * determine the desired screen size and refresh rate.
 * If we are also not using "GuiSizeForTV" we also resize
 * the video output window.
 *
 * \param width,height Resolution of the video we will be playing
 */
void MythVideoOutputGPU::ResizeForVideo(QSize Size)
{
    if (!m_display)
        return;
    if (!m_display->UsingVideoModes())
        return;

    if (Size.isEmpty())
    {
        Size = GetVideoDispDim();
        if (Size.isEmpty())
            return;
    }

    float rate = m_dbDisplayProfile ? m_dbDisplayProfile->GetOutput() : 0.0F;

    bool hide = m_display->NextModeIsLarger(Size);
    MythMainWindow* window = GetMythMainWindow();
    if (hide)
        window->hide();

    if (m_display->SwitchToVideo(Size, static_cast<double>(rate)))
    {
        // Switching to custom display resolution succeeded
        // Make a note of the new size
        QString source;
        double aspect = m_display->GetAspectRatio(source);
        LOG(VB_PLAYBACK, LOG_INFO, LOC + QString("Aspect ratio: %1 (%2)")
            .arg(aspect).arg(source));
        SetDisplayAspect(static_cast<float>(aspect));
        SetWindowSize(m_display->GetResolution());

        bool fullscreen = !UsingGuiSize();

        // if width && height are zero users expect fullscreen playback
        if (!fullscreen)
        {
            int gui_width = 0;
            int gui_height = 0;
            gCoreContext->GetResolutionSetting("Gui", gui_width, gui_height);
            fullscreen |= (0 == gui_width && 0 == gui_height);
        }

        if (fullscreen)
        {
            QSize sz = m_display->GetResolution();
            QRect display_visible_rect = QRect(GetMythMainWindow()->geometry().topLeft(), sz);
            if (HasMythMainWindow())
            {
                if (hide)
                {
                    window->Show();
                    hide = false;
                }
                GetMythMainWindow()->MoveResize(display_visible_rect);
            }
        }
    }
    if (hide)
        window->Show();
}
