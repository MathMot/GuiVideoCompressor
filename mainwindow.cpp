#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "QFileDialog"
#include "QProcess"
#include "QStandardPaths"
#include "QJsonDocument"
#include "QJsonObject"
#include "QJsonArray"
#include "QToolTip"
#include "QHelpEvent"
#include "QSettings"
#include "QMessageBox"
#include <QDesktopServices>
#include <QUrl>
#include <QStyleFactory>

//QSettings default values
double defaultSizeLimit = 50;
int defaultIndexSizeType = 5;
QString defaultVideoFolder = QDir::homePath();
QString defaultOutputFolder = "";
int defaultIntIndex = 0;

//Default window values
int windowHeight;
int windowWidth;
bool isExtended = false;


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    windowHeight = this->height();
    windowWidth = this->width();

    //Prevents the window from being resized by the user
    this->setWindowFlag(Qt::MSWindowsFixedSizeDialogHint, true);
    this->setWindowFlag(Qt::WindowMaximizeButtonHint, false);
    this->statusBar()->setSizeGripEnabled(false);

    //Sets the different themes from the user's system

    ui->comboBox_theme->addItems(QStyleFactory::keys());

    ui->videoList->viewport()->installEventFilter(this);

    QCoreApplication::setOrganizationName("MathMoth");
    QCoreApplication::setApplicationName("GUIVideoCompressor");

    QSettings settings;

    if (settings.contains("inputFolder")){
        defaultVideoFolder = settings.value("inputFolder").toString();
    }

    if (settings.contains("outputFolder")){
        defaultOutputFolder = settings.value("outputFolder").toString();
    }

    if (settings.contains("sizeLimit")){
        QString t = settings.value("sizeLimit").toString();

        if (t.toDouble()){
            defaultSizeLimit = t.toDouble();
        }
    }

    if (settings.contains("sizeLimitType")){
        QString t = settings.value("sizeLimitType").toString();

        if (t.toInt()){
            defaultIndexSizeType = t.toInt();
        }
    }

    if (settings.contains("themeIndex")){
        QString t = settings.value("themeIndex").toString();

        if (t.toInt()){
            defaultIntIndex = t.toInt();
        }
    }
    ui->lineEdit_outputFolder->setText(defaultOutputFolder);
    ui->comboBox_finalSizeType->setCurrentIndex(defaultIndexSizeType);
    ui->doubleSpinBox_finalSize->setValue(defaultSizeLimit);
    ui->comboBox_theme->setCurrentIndex(defaultIntIndex);

    //Sets the theme
    qApp->setStyle(ui->comboBox_theme->currentText());

    //Creates if needed the folder for ffmpeg
    QDir parentFolder(QCoreApplication::applicationDirPath());
    parentFolder.cdUp();

    if (!parentFolder.exists("deps")){
        parentFolder.mkdir("deps");
    }

    RefreshDependencies();
}

MainWindow::~MainWindow()
{
    delete ui;
}



struct LogInfo {
    QString overrideMessage = ""; // if set, will override
    QString l1 = "--Status--";
    QString fileName = "temp.mp4";
    QString fileIndex = "";
    QString fileCount = "";
    QString step = "";//Retrieving video data | Pass 1 | Pass 2
    QString targetSize = "";
    QString encoder = "lib264";
    QString ffmpegOutput = "";

};

QList<VideoJob> videoJobs;

QProcess ffmpegThumbnailProcess;

LogInfo currentLog;

int totalVideosCompressing;

//To prevent false positive with how ffmpeg outputs updates
int oldProgress;

bool abort_pressed = false;

QString ffmpegPath = "ffmpeg";
QString ffprobePath = "ffprobe";

void MainWindow::on_button_AddVideos_pressed()
{

    QString videoFolder = defaultVideoFolder;
    QSettings settings;

    if (!QDir(defaultVideoFolder).exists()){
        videoFolder = QDir::homePath();
    }
    //Open the dialog and select all of the files
    QStringList files = QFileDialog::getOpenFileNames(
        this,
        "Select mp4 files",
        videoFolder,
        "Fichiers MP4 (*.mp4)"
        );



    if (files.isEmpty())return;

    //Save into the settings the path of the folder
    QString folderPath = QFileInfo(files.at(0)).absolutePath();
    settings.setValue("inputFolder",folderPath);

    //Creates if needed and store the cache folder for the thumbnails
    QString cachePath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QString thumbnailDir = cachePath + "/thumbnails";
    QDir().mkpath(thumbnailDir);

    //Also creates the cache path for the pass encoding files of ffmpeg
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/tempffmpeg/");

    //Add every file into the list, without any thumbnail
    for (const QString &filePath : files.toVector()){

        QFileInfo fileName(filePath);

        // Check if the list doesn't already contain the element
        bool alreadyExisting = false;
        for (int i = 0 ; i < ui->videoList->count(); i ++){
            QListWidgetItem *item = ui->videoList->item(i);
            if (item->data(Qt::UserRole) == filePath){
                alreadyExisting = true;
                break;
            }
        }

        if (alreadyExisting)break;

        //Creating/Adding the item to the list
        QListWidgetItem *item = new QListWidgetItem();
        item->setText(fileName.fileName());
        item->setToolTip(filePath);
        item->setData(Qt::UserRole,filePath);

        ui->videoList->addItem(item);

        //Setting the icon to the generated thumbnail
        QString tempThumbnailPath = thumbnailDir + "/" + fileName.fileName() + ".jpg";

        //Creates the process to get the thumbnail
        QProcess *process = new QProcess(this);

        //Called when the process is finished, to add the generated thumbnail to the item as an icon
        connect(process, &QProcess::finished, this, [=](int exitCode, QProcess::ExitStatus status){
            if (exitCode == 0) {
                QPixmap pixmap(tempThumbnailPath);
                pixmap = pixmap.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                item->setIcon(QIcon(pixmap));

                //Add the image to the tooltip using html thingie
                QString tooltip =
                    "<html>"
                    "<b>"+item->toolTip()+"<b>" +
                    "<img src=\"file:///" + tempThumbnailPath + "\" width=\"400\"/><br/>"+
                    "</html>";

                item->setToolTip(tooltip);

                // QFile thumbnail(tempThumbnailPath);
                // if (thumbnail.exists())
                //     thumbnail.moveToTrash();

            } else {
                currentLog.overrideMessage = "FFMPEG error while retrieving the thumbnail of the videos !";

            }
            process->deleteLater();
        });

        //FFMPEG command to generate the thumbnail
        QStringList args = {
            "-y",
            "-i", filePath,
            "-ss", "00:00:01",
            "-vframes", "1",
            tempThumbnailPath
        };

        process->start(ffmpegPath, args);

    }
}


//Removes the selected items of the list
void MainWindow::on_button_removeSelectedVideo_pressed()
{

    for(const QListWidgetItem *item : ui->videoList->selectedItems()){
        delete ui->videoList->takeItem(ui->videoList->row(item));
    }

}

//select the output folder
void MainWindow::on_toolButton_choseOutputFolder_pressed()
{

    QString outputFolder = defaultOutputFolder;
    QSettings settings;

    if (!QDir(defaultOutputFolder).exists()){
        outputFolder = QDir::homePath();
    }


    QString chosenDir = QFileDialog::getExistingDirectory(
        this,
        tr("$Choose the output folder of the files"),
        outputFolder,           // Default folder, TODO from config
        QFileDialog::ShowDirsOnly
        );

    settings.setValue("outputFolder",chosenDir);
    ui->lineEdit_outputFolder->setText(chosenDir);
}


void MainWindow::on_checkBox_outputFPS_pressed()
{
    ui->spinBox_outputFPS->setEnabled(!ui->checkBox_outputFPS->isChecked());
}


void MainWindow::on_pushButton_compress_pressed()
{

    //Saving the settings of the size thingie

    QSettings settings;
    settings.setValue("sizeLimit",ui->doubleSpinBox_finalSize->value());
    settings.setValue("sizeLimitType",ui->comboBox_finalSizeType->currentIndex());
    currentLog.overrideMessage = "";
    //Before compressing, checking if there is any value in the list + if the output folder exists

    if (ui->videoList->count() == 0){
        currentLog.overrideMessage = "No video selected !";
        UpdateInfo();
        return;
    }

    QDir outputFolder(ui->lineEdit_outputFolder->text());
    if ( ui->lineEdit_outputFolder->text() == "" || !outputFolder.exists()){
        currentLog.overrideMessage = "The output folder is invalid !";
        UpdateInfo();
        return;
    }

    if (ui->doubleSpinBox_finalSize->value() <= 0){
        currentLog.overrideMessage = "You must set a valid fps limit !";
        UpdateInfo();
        return;
    }

    if (ffmpegPath == "" || ffprobePath == ""){
        currentLog.overrideMessage = "Can't detect a valid ffmpeg or ffprobe instance, check the settings to set them !";
        UpdateInfo();
        return;
    }

    // Starts by getting every videoinformation using ffprobe
    oldProgress = 0;

    abort_pressed = false;

    videoJobs.clear();

    currentLog.overrideMessage = "Preparing videos...";
    UpdateInfo();

    for (int i = 0 ; i < ui->videoList->count(); i ++){
        QListWidgetItem *item = ui->videoList->item(i);

        QString filePath = item->data(Qt::ItemDataRole::UserRole).toString();
        QString fileName = QFileInfo(filePath).fileName();

        //Check if no output path is the same
        bool isTheSame = false;
        for(int j = 0; j < videoJobs.count() ; j++){
            if (videoJobs.at(j).outputPath.endsWith(fileName)){
                isTheSame = true;
            }
        }

        VideoJob videoJob;
        videoJob.inputPath = filePath;

        //If outputPath is the same, add i at the end of the name of the file
        // ex : clip.mp4 => clip1.mp4, or clip2.mp4
        QString outputPath;
        if (isTheSame){
            QString temp = QFileInfo(videoJob.inputPath).fileName();
            temp.chop(4);
            temp += QString::number(i) + ".mp4";
            outputPath = outputFolder.absolutePath() + "/" +  temp;
        }else{
            outputPath = outputFolder.absolutePath() + "/" + QFileInfo(videoJob.inputPath).fileName();
        }

        videoJob.outputPath = outputPath;
        videoJobs.append(videoJob);
    }


    qDebug() << "1";
    totalVideosCompressing = videoJobs.count();
    currentLog.fileCount = QString::number(videoJobs.count());
    Job1StartLoop();

}


void MainWindow::UpdateInfo(){

    LogInfo c = currentLog;
    QString txt = "";

    if (abort_pressed){
        c.overrideMessage = "ABORTED";
    }

    if (c.overrideMessage == ""){

        txt = c.l1
              + "\nFile Name : "+c.fileName
              +"\n File "+c.fileIndex + "/"+c.fileCount
              + "\nCurrent Step : "+c.step
              + "\nTarget Size : "+c.targetSize;

    }else{
        txt = c.overrideMessage;
    }
    //qDebug() << "Updating labelLog to "+txt;
    ui->label_log->setText(txt);

    //ui->label_log_ffmpeg->setText( (c.overrideMessage == "" ? "FFMPEG Output :\n"+c.ffmpegOutput : ""));


    //Updating progress bar, god help me
    int passNumber = (c.step == "Pass 2" ? 2 : 1);
    int totalFiles = c.fileCount.toInt();

    int fileIndex0 = c.fileIndex.toInt() - 1;

    double stepValue = 100.0 / (totalFiles * 2);

    double progress = (fileIndex0 * 2 + (passNumber - 1)) * stepValue;

    //Adding the local progress to the progress

    if (videoJobs.isEmpty())return;

    VideoJob currentJob = CurrentVJ();

    float totalVideoDuration =  currentJob.videoInfo.duration; // in seconds
    QString ffmpegOutput = c.ffmpegOutput;
    // frame=  123 fps= 60 q=28.0 size=    1024kB time=00:00:05.12 bitrate=1638.4kbits/s speed=1.00x

    bool gotTime = false;
    float relativeProgress = 0;
    QStringList indexes = ffmpegOutput.split("time=");
    if ( ffmpegOutput.startsWith("frame") && indexes.count() >= 2){
        ffmpegOutput =  indexes[1];
        QStringList spaceIndexes = ffmpegOutput.split(" ");
        if (spaceIndexes.count() >= 1){
            ffmpegOutput = spaceIndexes[0];
            gotTime = true;
        }
    }

    if (gotTime){
        QStringList parts = ffmpegOutput.split(':');
        if (parts.size() == 3){
            int hour = parts[0].toInt();
            int minutes = parts[1].toInt();
            double seconds = parts[2].toDouble();

            seconds += minutes*60 + hour*3600;

            if (seconds > 2){

                float mult = 100/totalVideoDuration;

                double diff = (seconds*mult) / (totalVideoDuration*mult);

                relativeProgress = (diff * 100) / (c.fileCount.toInt() * 2);
            }

            //ui->label_log->setText( ffmpegOutput + " - "+ QString::number(seconds) + "/" + QString::number(totalVideoDuration) + " - " + QString::number(diff));

        }
    }

    int totalProgress = progress + relativeProgress;

    totalProgress = (totalProgress < oldProgress ? oldProgress : totalProgress);

    oldProgress = totalProgress;
    if (abort_pressed) totalProgress = 0;

    ui->progressBar->setValue(static_cast<int>(totalProgress));


    //ui->progressBar->setValue(minProgress);
}

//Starts/Restarts the loop of compression
void MainWindow::Job1StartLoop(){

    if (videoJobs.isEmpty()){
        currentLog.overrideMessage = "Succesfully compressed all of the files !";
        UpdateInfo();
        ui->progressBar->setValue(100);
        return;
    }
    Job2GetVideoData();

}

void MainWindow::Job2GetVideoData(){

    QProcess *ffprobe = new QProcess(this);


    connect(ffprobe, &QProcess::finished, this, [=](int exitCode, QProcess::ExitStatus){
        qDebug() << "finished ffprobe with exit code";
        qDebug() << exitCode;
        if (exitCode != 0) {
            qWarning() << "Error FFMPEG";
            ffprobe->deleteLater();
            return;
        }
        QByteArray output = ffprobe->readAllStandardOutput();

        QJsonDocument doc = QJsonDocument::fromJson(output);

        if (doc.isObject()){
            QJsonObject root = doc.object();

            // Duration
            if (root.contains("format")) {
                QJsonObject format = root["format"].toObject();
                if (format.contains("duration"))
                    CurrentVJ().videoInfo.duration = format["duration"].toString().toDouble();
            }

            // Video stream
            if (root.contains("streams")) {
                QJsonArray streams = root["streams"].toArray();
                for (const QJsonValue& val : streams) {
                    QJsonObject stream = val.toObject();
                    QString codecType = stream["codec_type"].toString();
                    if (codecType == "video") {
                        // FPS
                        QString fpsStr = stream["avg_frame_rate"].toString(); //ex: "30000/1001"
                        QStringList parts = fpsStr.split('/');
                        if (parts.size() == 2)
                            CurrentVJ().videoInfo.fps = parts[0].toDouble() / parts[1].toDouble();

                        CurrentVJ().videoInfo.width = stream["width"].toInt();
                        CurrentVJ().videoInfo.height = stream["height"].toInt();
                        break;
                    }
                }

                // Audio stream
                for (const QJsonValue& val : streams) {
                    QJsonObject stream = val.toObject();
                    QString codecType = stream["codec_type"].toString();
                    if (codecType == "audio") {
                        CurrentVJ().videoInfo.audioBitrateKbps = stream["bit_rate"].toString().toInt() / 1000; //kbps
                        break;
                    }
                }
            }}

        //here we got the full videoInfo set, we start the Pass1
        Job3Pass1();


    });


    currentLog.overrideMessage = "";
    currentLog.fileName = QFileInfo(CurrentVJ().inputPath).fileName();
    currentLog.fileIndex = QString::number(totalVideosCompressing - videoJobs.count() + 1);
    currentLog.step = "Retrieving video data";
    currentLog.targetSize = QString::number(ui->doubleSpinBox_finalSize->value() ) + ui->comboBox_finalSizeType->currentText();
    UpdateInfo();

    QStringList args;
    args << "-v" << "error"
         << "-show_entries"
         << "format=duration:stream=index,codec_type,avg_frame_rate,width,height,bit_rate,sample_rate,channels"
         << "-of" << "json"
         << CurrentVJ().inputPath;

    ffprobe->start(ffprobePath, args);

}

void MainWindow::Job3Pass1(){

    currentLog.step = "Pass 1";
    UpdateInfo();

    QProcess *ffmpegPass1 = new QProcess(this);

    connect(ffmpegPass1, &QProcess::readyReadStandardError, this, [=](){

        if (abort_pressed){
            ffmpegPass1->deleteLater();
            UpdateInfo();
            return;
        }
        currentLog.ffmpegOutput = ffmpegPass1->readAllStandardError();
        UpdateInfo();
    });


    connect(ffmpegPass1, &QProcess::finished, this, [=](int exitCode, QProcess::ExitStatus){
        qDebug() << "finished ffprobe with exit code";
        qDebug() << exitCode;
        if (exitCode != 0) {
            qWarning() << "Error FFMPEG";
            ffmpegPass1->deleteLater();
            return;
        }

        Job3Pass2();
        //Pass2 !!
    });


    //QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/tempffmpeg/"

    //Making the Pass1
    //Maths to get the final video bitrate + other stuff

    QString passlogPath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/tempffmpeg/ffmpeg_pass-0.log";
    QDir().mkpath(passlogPath);
    long long unsigned targetBitSize;
    long long unsigned videoBitratebps;

    double size = ui->doubleSpinBox_finalSize->value();
    QString type = ui->comboBox_finalSizeType->currentText();

    //Sorry abastien but not switch for QString ?
    if (type == "b"){
        targetBitSize = size;
    }else if (type == "B"){
        targetBitSize = size * 8;
    }else if (type == "Kb"){
        targetBitSize = size * 1000;
    }else if (type == "KB"){
        targetBitSize = size * 1000 * 8;
    }else if (type == "Mb"){
        targetBitSize = size * 1000000;
    }else if (type == "MB"){
        targetBitSize = size * 1000000 * 8;
    }else if (type == "Gb"){
        targetBitSize = size * 1000000000;
    }else if (type == "GB"){
        targetBitSize = size * 1000000000 * 8;
    }

    //Calculating the video bitratebps
    long long unsigned video_bits = targetBitSize - (CurrentVJ().videoInfo.audioBitrateKbps * 1000 * CurrentVJ().videoInfo.duration);
    videoBitratebps = video_bits/CurrentVJ().videoInfo.duration;
    long long unsigned video_bitrate_kbps = std::roundl(videoBitratebps / 1000);
    CurrentVJ().videoInfo.videoBitrateKbps = video_bitrate_kbps;
    double targetFPS = CurrentVJ().videoInfo.fps;

    if (ui->spinBox_outputFPS->isEnabled() && ui->spinBox_outputFPS->value() < targetFPS){
        targetFPS = ui->spinBox_outputFPS->value();
        CurrentVJ().videoInfo.fps = targetFPS;
    }

    QStringList args;
    args << "-y" << "-i" << CurrentVJ().inputPath
         << "-r" << QString::number(targetFPS) << "-c:v" << "libx264"
         << "-b:v" <<  QString::number(video_bitrate_kbps)+"k"
         << "-pass"<<"1"<< "-passlogfile" <<passlogPath << "-an"
         << "-f"<<"null"<< "NUL";
    ffmpegPass1->start(ffmpegPath, args);


}

void MainWindow::Job3Pass2(){

    currentLog.step = "Pass 2";
    QProcess *ffmpegPass2 = new QProcess(this);


    connect(ffmpegPass2, &QProcess::readyReadStandardError, this, [=](){
        if (abort_pressed){
            ffmpegPass2->deleteLater();
            UpdateInfo();
            return;
        }
        currentLog.ffmpegOutput = ffmpegPass2->readAllStandardError();
        UpdateInfo();
    });

    connect(ffmpegPass2, &QProcess::finished, this, [=](int exitCode, QProcess::ExitStatus){
        qDebug() << "finished ffmpegPass2 with exit code";
        qDebug() << exitCode;
        if (exitCode != 0) {
            qWarning() << "Error FFMPEG";
            ffmpegPass2->deleteLater();
            return;
        }
        videoJobs.takeFirst();
        Job1StartLoop();
        //Pass2 !!
    });


    //QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/tempffmpeg/"

    //Making the Pass1
    //Maths to get the final video bitrate + other stuff

    QString passlogPath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/tempffmpeg/ffmpeg_pass-0.log";

    QStringList args;
    args << "-y" << "-i" << CurrentVJ().inputPath
         << "-r" << QString::number(CurrentVJ().videoInfo.fps)
         <<"-c:v" <<"libx264" <<"-b:v"
         << QString::number(CurrentVJ().videoInfo.videoBitrateKbps)+"k"
         << "-pass" << "2" << "-passlogfile" << passlogPath<< "-c:a"
         <<"aac"<<"-b:a" << QString::number(CurrentVJ().videoInfo.audioBitrateKbps) + "k"
         << "-preset" <<"slow"<<"-profile:v"<<"high"
         <<"-level"<<"4.2" << CurrentVJ().outputPath;


    ffmpegPass2->start(ffmpegPath, args);
}




VideoJob& MainWindow::CurrentVJ(){
    return videoJobs[0];
}


void MainWindow::on_pushButton_abort_pressed()
{
    abort_pressed = true;
    oldProgress = 0;
    ui->progressBar->setValue(0);
}


void MainWindow::on_radioButton_clearOutputFolder_pressed()
{
    if (ui->radioButton_clearOutputFolder->isChecked())return;

    QMessageBox msgBox;
    msgBox.setText("By checking this, every mp4 files in the selected directory will be deleted when compressing.");
    msgBox.exec();
    ui->radioButton_clearOutputFolder->setChecked(true);
}



void MainWindow::on_pushButton_open_output_folder_pressed()
{
    QSettings settings;
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(settings.value("outputFolder").toString()));
}


void MainWindow::on_pushButton_open_settings_pressed()
{
    int extendValue = (isExtended ? -200 : 200);
    isExtended = !isExtended;

    resize(windowWidth, windowHeight + extendValue);
    windowHeight += extendValue;

    this->ui->pushButton_open_settings->setText(isExtended ? "Close Dependencies Settings":"Open Dependencies Settings");
}


void MainWindow::on_pushButton_open_deps_folder_pressed()
{
    QDir parentFolder(QCoreApplication::applicationDirPath());
    parentFolder.cdUp();

    QDesktopServices::openUrl(
        QUrl::fromLocalFile(parentFolder.absolutePath() + "/deps/"));
}


void MainWindow::RefreshDependencies(){

    QString ffmpegFileName = "ffmpeg";

    QString ffprobeFileName = "ffprobe";

#ifdef Q_OS_WIN
    ffmpegFileName += ".exe";
    ffprobeFileName += ".exe";
#endif

    //Checking if ffmpeg & ffprobe are detected on the deps folder


    QDir parentFolder(QCoreApplication::applicationDirPath());
    parentFolder.cdUp();

    parentFolder.cd("deps");

    bool ffmpegLocal = TestProcess(parentFolder.absolutePath() + "/"+ffmpegFileName);

    bool ffprobeLocal = TestProcess(parentFolder.absolutePath() + "/"+ffprobeFileName);

    ffmpegPath = ffmpegLocal ?  parentFolder.absolutePath() + "/"+ffmpegFileName : "";
    ffprobePath = ffprobeLocal ?  parentFolder.absolutePath() + "/"+ffprobeFileName : "";

    bool ffmpegGlobal = false;
    bool ffprobeGlobal = false;

    if (!ffmpegLocal){
        if (TestProcess("ffmpeg")){
            ffmpegGlobal = true;
            ffmpegPath = "ffmpeg";
        }
    }

    if (!ffprobeLocal){
        if (TestProcess("ffprobe")){
            ffprobeGlobal = true;
            ffprobePath = "ffprobe";
        }
    }


    QString ffmpegButtonLabel = "FFMPEG is not detected !";
    QString ffprobeButtonLabel = "FFPROBE is not detected !";

    if (ffmpegLocal){
        ffmpegButtonLabel = "FFMPEG is detected !\n(locally)";
    }else if (ffmpegGlobal){
        ffmpegButtonLabel = "FFMPEG is detected !\n(globally)";
    }

    if (ffprobeLocal){
        ffprobeButtonLabel = "FFPROBE is detected !\n(locally)";
    }else if (ffprobeGlobal){
        ffprobeButtonLabel = "FFPROBE is detected !\n(globally)";
    }

    this->ui->label_ffmpeg_detection->setText(ffmpegButtonLabel);
    this->ui->label_ffprobe_detection->setText(ffprobeButtonLabel);

    //All deps detected
    if (ffmpegPath != "" && ffprobePath != ""){
            this->ui->label_dependencies_check->setText("All dependencies loaded !");
                this->ui->label_dependencies_check->setStyleSheet("color: rgb(126, 255, 169);");
    }else{
        //Not all of the deps detected
        this->ui->label_dependencies_check->setText("Couldn't load dependencies\ncheck the settings for more info");
        this->ui->label_dependencies_check->setStyleSheet("color: rgb(255, 120, 120);");
    }




}

bool MainWindow::TestProcess(QString processName){

    qDebug() << "Testing if "+processName;
    QProcess process;
    process.start(processName, { "-version" });

    if (!process.waitForStarted(2000)) {
        //qDebug() << "it doesn't exists ";
        return false;
    }

    process.waitForFinished(2000);

    if (process.exitStatus() == QProcess::NormalExit &&
        process.exitCode() == 0) {
        //qDebug() << "it exists !";
        return true;
    }
    //qDebug() << "it doesn't exists ";
    return false;

}

void MainWindow::on_pushButton_3_pressed()
{
    RefreshDependencies();
}


void MainWindow::on_comboBox_theme_currentIndexChanged(int index)
{
    QSettings settings;
    qApp->setStyle(ui->comboBox_theme->currentText());
        settings.setValue("themeIndex",index);
}

