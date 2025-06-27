#include "waka_plugin.h"
#include "waka_constants.h"
#include "waka_options.h"
#include "waka_options_page.h"

#include <cstring>

#include <coreplugin/icore.h>
#include <coreplugin/icontext.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>
#include <coreplugin/statusbarmanager.h>
#include <coreplugin/messagemanager.h>

#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/projecttree.h>
#include <projectexplorer/project.h>

#include <extensionsystem/iplugin.h>
#include <texteditor/texteditor.h>

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonObject>
#include <QJsonDocument>
#include <QToolButton>
#include <QTimer>
#include <QThread>

namespace Wakatime {
namespace Internal {

WakaPlugin::WakaPlugin():_cliIsSetup(false){}

WakaPlugin::~WakaPlugin()
{
    // Unregister objects from the plugin manager's object pool
    // Delete members
}

void WakaPlugin::showMessagePrompt(const QString str){
    if(_wakaOptions.isNull())
        return;

    if(_wakaOptions->isDebug())
        Core::MessageManager::writeSilently(str);
}

QDir WakaPlugin::getWakaCLILocation(){
    QString default_path = QDir::homePath().append("/.wakatime");
    return default_path;
}

bool WakaPlugin::checkIfWakaCLIExist(){
    return getWakaCLILocation()
            .entryList().filter("wakatime-cli").isEmpty()==false;
}

bool WakaPlugin::initialize(const QStringList &arguments, QString *errorString)
{
    // Register objects in the plugin manager's object pool
    // Load settings
    // Add actions to menus
    // Connect to other plugins' signals
    // In the initialize function, a plugin can be sure that the plugins it
    // depends on have initialized their members.

    Q_UNUSED(arguments)
    Q_UNUSED(errorString)

    _wakaOptions.reset(new WakaOptions);
    if(_page.isNull()){
        _page.reset(new WakaOptionsPage(_wakaOptions,this));
    }


    _cliGetter = new CliGetter();

    _cliGettingThread = new QThread(this);
    _cliGetter->moveToThread(_cliGettingThread);

    connect(this,&WakaPlugin::doneGettingCliAndSettingItUp,
            this,&WakaPlugin::onDoneSettingUpCLI);


    //Heartbeat sending signal slot combo
    connect(this,&WakaPlugin::sendHeartBeat,
            _cliGetter,&CliGetter::startHearBeat);
    //set the status icon flashing during hearbeats
    connect(this,&WakaPlugin::sendHeartBeat,[self=this](QString file){
        if(self->_heartBeatButton.isNull()==false){
            self->_heartBeatButton->setDisabled(false);
            QTimer::singleShot(200,[self](){
                self->_heartBeatButton->setDisabled(true);
            });
        }
    });
    //for showing prompts
    connect(_cliGetter,&CliGetter::promptMessage,this,&WakaPlugin::showMessagePrompt);

    //check if has wakatime-cli in path
    //if not then try download it based of the users operating system
    if(!checkIfWakaCLIExist()){
        _cliGetter->connect(_cliGettingThread,&QThread::started,
                            _cliGetter,&CliGetter::startGettingAssertUrl);
        connect(_cliGetter,&CliGetter::doneSettingWakaTimeCli,
                [plugin = this](){
            plugin->_cliIsSetup=true;
            emit plugin->doneGettingCliAndSettingItUp();
        });
    }else{
        emit this->doneGettingCliAndSettingItUp();
    }

    _cliGettingThread->start();
    return true;
}

void WakaPlugin::onDoneSettingUpCLI(){

    //!TODO
    //check if is latest version
    //check if user has asked for updated version
    //if so, then try update the version of wakatime-cli


    showMessagePrompt("WakatimeCLI setup");

    connect(_wakaOptions.data(), &WakaOptions::inStatusBarChanged,
            this, &WakaPlugin::onInStatusBarChanged);

    connect(Core::EditorManager::instance(), &Core::EditorManager::aboutToSave,
            this, &WakaPlugin::onAboutToSave);
    connect(Core::EditorManager::instance(), &Core::EditorManager::currentEditorAboutToChange,
            this, &WakaPlugin::onEditorAboutToChange);
    connect(Core::EditorManager::instance(), &Core::EditorManager::currentEditorChanged,
            this, &WakaPlugin::onEditorChanged);

    onInStatusBarChanged();

    showMessagePrompt("Waka plugin initialized!");
}

void WakaPlugin::extensionsInitialized()
{
    // Retrieve objects from the plugin manager's object pool
    // In the extensionsInitialized function, a plugin can be sure that all
    // plugins that depend on it are completely initialized.
}

ExtensionSystem::IPlugin::ShutdownFlag WakaPlugin::aboutToShutdown()
{
    // Save settings
    // Disconnect from signals that are not needed during shutdown
    // Hide UI (if you add UI that is not in the main window directly)
    if(_cliGettingThread->isRunning()==true){
        _cliGettingThread->quit();
    }
    _cliGettingThread->terminate();
    return SynchronousShutdown;
}

void WakaPlugin::onInStatusBarChanged()
{
    if(_heartBeatButton.isNull())
    {
        _heartBeatButton.reset(new QToolButton());
        _heartBeatButton->setIcon(QIcon(":/heartbeat.png"));
        _heartBeatButton->setDisabled(true);
    }


    if(_wakaOptions->inStatusBar())
    {
        Core::StatusBarManager::addStatusBarWidget(
                    _heartBeatButton.data(),
                    Core::StatusBarManager::RightCorner);
    }else{
        Core::StatusBarManager::destroyStatusBarWidget(
                    _heartBeatButton.data());
    }
}

void WakaPlugin::onEditorAboutToChange(Core::IEditor *editor)
{
    if(!editor)
        return;

    disconnect(TextEditor::TextEditorWidget::currentTextEditorWidget(),
               &TextEditor::TextEditorWidget::textChanged,
               this, &WakaPlugin::onEditorStateChanged);
}

void WakaPlugin::onEditorChanged(Core::IEditor *editor)
{
    if(!editor)
        return;

    connect(TextEditor::TextEditorWidget::currentTextEditorWidget(), &TextEditor::TextEditorWidget::textChanged, this, &WakaPlugin::onEditorStateChanged);
    emit this->sendHeartBeat(editor->document()->filePath().toString());
}

void WakaPlugin::onAboutToSave(Core::IDocument *document)
{
    emit sendHeartBeat(document->filePath().toString());
}

void WakaPlugin::onEditorStateChanged()
{
    emit sendHeartBeat(Core::EditorManager::currentDocument()->filePath().toString());
}

} // namespace Internal
} // namespace QtCreatorWakatime
