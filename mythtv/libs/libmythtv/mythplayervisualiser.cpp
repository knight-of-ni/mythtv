// MythTV
#include "mythcorecontext.h"
#include "audioplayer.h"
#include "mythmainwindow.h"
#include "tv_play.h"
#include "mythplayervisualiser.h"

MythPlayerVisualiser::MythPlayerVisualiser(MythMainWindow *MainWindow, TV *Tv,
                                           PlayerContext *Context, PlayerFlags Flags)
  : MythPlayerUIBase(MainWindow, Tv, Context, Flags)
{
    connect(m_tv, &TV::EmbedPlayback, this, &MythPlayerVisualiser::EmbedVisualiser);
}

MythPlayerVisualiser::~MythPlayerVisualiser()
{
    MythPlayerVisualiser::DestroyVisualiser();
}

void MythPlayerVisualiser::PrepareVisualiser()
{
    if (!(m_visual && m_painter) || (m_embedding && m_embedRect.isEmpty()))
        return;
    if (m_visual->NeedsPrepare())
        m_visual->Prepare(m_embedding ? m_embedRect : m_mainWindow->GetUIScreenRect());
}

void MythPlayerVisualiser::RenderVisualiser()
{
    if (!(m_visual && m_painter) || (m_embedding && m_embedRect.isEmpty()))
        return;
    m_visual->Draw(m_embedding ? m_embedRect : m_mainWindow->GetUIScreenRect(), m_painter, nullptr);
}

void MythPlayerVisualiser::DestroyVisualiser()
{
    delete m_visual;
    m_visual = nullptr;
}

bool MythPlayerVisualiser::CanVisualise()
{
    return VideoVisual::CanVisualise(&m_audio, m_render);
}

bool MythPlayerVisualiser::IsVisualising()
{
    return m_visual != nullptr;
}

QString MythPlayerVisualiser::GetVisualiserName()
{
    if (m_visual)
        return m_visual->Name();
    return QString();
}

QStringList MythPlayerVisualiser::GetVisualiserList()
{
    return VideoVisual::GetVisualiserList(m_render->Type());
}

bool MythPlayerVisualiser::EnableVisualiser(bool Enable, const QString &Name)
{
    if (!Enable)
    {
        DestroyVisualiser();
        return false;
    }
    DestroyVisualiser();
    m_visual = VideoVisual::Create(Name, &m_audio, m_render);
    return m_visual != nullptr;
}

/*! \brief Enable visualisation if possible, there is no video and user has requested.
*/
void MythPlayerVisualiser::AutoVisualise(bool HaveVideo)
{
    if (!m_audio.HasAudioIn() || HaveVideo)
        return;

    if (!CanVisualise() || IsVisualising())
        return;

    auto visualiser = gCoreContext->GetSetting("AudioVisualiser", "");
    if (!visualiser.isEmpty())
        EnableVisualiser(true, visualiser);
}

void MythPlayerVisualiser::EmbedVisualiser(bool Embed, const QRect &Rect)
{
    m_embedRect = Rect;
    m_embedding = Embed;
}

bool MythPlayerVisualiser::IsEmbedding()
{
    return m_embedding;
}
