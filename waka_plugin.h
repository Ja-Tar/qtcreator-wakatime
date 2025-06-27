#pragma once

#include "waka_global.h"
#include "waka_constants.h"
#include "cligetter.h"

#include <extensionsystem/iplugin.h>

#include <QPointer>
#include <QFile>

class QNetworkAccessManager;
class QNetworkReply;
class QToolButton;

namespace Core {
    class IEditor;
    class IDocument;
}

namespace Wakatime {
namespace Internal {

class WakaOptions;
class WakaOptionsPage;
class CliGetter;

// For using OSInfo
using namespace Wakatime::Constants;

class WakaPlugin : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "Wakatime.json")

public:

    WakaPlugin();
    ~WakaPlugin() override;
    void showMessagePrompt(const QString str);

    bool initialize(const QStringList &arguments, QString *errorString); // usuń override
    void extensionsInitialized() override;
    ShutdownFlag aboutToShutdown() override;

    void trySendHeartbeat(const QString &entry, bool isSaving);
    static QDir getWakaCLILocation();

private:
    bool checkIfWakaCLIExist();

private slots:
    void onEditorAboutToChange(Core::IEditor *editor);
    void onEditorChanged(Core::IEditor *editor);
    void onAboutToSave(Core::IDocument *document);
    void onEditorStateChanged();

    void onInStatusBarChanged();

    void onDoneSettingUpCLI();

signals:
    void doneGettingCliAndSettingItUp();
    void sendHeartBeat(QString file);

private:
    qint64 _lastTime = 0;
    QString _lastEntry{""};

    CliGetter *_cliGetter;//managing accessing wakatime-cli
    bool _cliIsSetup;

    QString _ignore_patern;
    QThread *_cliGettingThread;
    QSharedPointer<QToolButton> _heartBeatButton;
    QSharedPointer<WakaOptions> _wakaOptions;
    QSharedPointer<WakaOptionsPage> _page;
};


} // namespace Internal
} // namespace QtCreatorWakatime
