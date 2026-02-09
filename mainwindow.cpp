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

//Dynamic dependencies pathing
QString ffmpegPath = "ffmpeg";
QString ffprobePath = "ffprobe";

// Used to save data between the different compression jobs (retrieving data, pass 1, etc)
struct LogInfo {
    QString overrideMessage = ""; // if set, will override
    QString l1 = "--Status--";
    QString fileName = "temp.mp4";
    QString fileIndex = "";
    QString fileCount = "";
    QString step = "";  //Retrieving video data | Pass 1 | Pass 2
    QString targetSize = "";
    QString encoder = "lib264";
    QString ffmpegOutput = "";

};

//Stores all of the jobs required to do ( each compression has 3)
QList<VideoJob> videoJobs;

//Current used LogInfo
LogInfo currentLog;

//Number of videos that needs to be compressed
int totalVideosCompressing;

//To prevent false positive with how ffmpeg outputs updates for the progress bar
// if the new value is smaller than the old one, it skips
int oldProgress;

//When the Abort button is pressed, this is changed and the current jobs spots it and aborts
bool abortPressed = false;


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

    //Enable the custom events for the thumbnails preview
    ui->videoList->viewport()->installEventFilter(this);

    QCoreApplication::setOrganizationName("MathMoth"); // me :)
    QCoreApplication::setApplicationName("GUIVideoCompressor");

    //Settings setup
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

    //Saves the default settings values
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

    //Check if the dependencies are ok before starting up
    refreshDependencies();
}

MainWindow::~MainWindow()
{
    delete ui;
}

//Adding clips to the selection
void MainWindow::on_button_AddVideos_pressed()
{
    QString videoFolder = defaultVideoFolder;
    QSettings settings;

    //Checking if the video path is set, otherwise the chosen path will be the home folder
    if (!QDir(defaultVideoFolder).exists()){
        videoFolder = QDir::homePath();
    }

    //Open the dialog and select all of the files
    QStringList files = QFileDialog::getOpenFileNames(
        this,
        "Select mp4 files",
        videoFolder,
        "*.mp4"
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

    //Saves the value to the settings
    settings.setValue("outputFolder",chosenDir);

    //Changes the ui display of the output folder
    ui->lineEdit_outputFolder->setText(chosenDir);
}

// to enable/disable the change output fps spot
void MainWindow::on_checkBox_outputFPS_pressed()
{
    ui->spinBox_outputFPS->setEnabled(!ui->checkBox_outputFPS->isChecked());
}

//Handles the information display + progressBar
void MainWindow::updateInfo(){

    //currentLog is the current LogInfo containing all of the current compression information
    LogInfo c = currentLog;
    QString txt = "";

    if (abortPressed){
        c.overrideMessage = "ABORTED";
    }

    //if there is no override message, default info display stuct
    if (c.overrideMessage == ""){
        txt = c.l1
              + "\nFile Name : "+c.fileName
              +"\n File "+c.fileIndex + "/"+c.fileCount
              + "\nCurrent Step : "+c.step
              + "\nTarget Size : "+c.targetSize;

    }else{
        txt = c.overrideMessage;
    }


    ui->label_log->setText(txt);

    //ui->label_log_ffmpeg->setText( (c.overrideMessage == "" ? "FFMPEG Output :\n"+c.ffmpegOutput : ""));


    //Updating progress bar, god help me
    //This gets the progress, dividing each actions
    int passNumber = (c.step == "Pass 2" ? 2 : 1);
    int totalFiles = c.fileCount.toInt();

    int fileIndex0 = c.fileIndex.toInt() - 1;

    double stepValue = 100.0 / (totalFiles * 2);

    double progress = (fileIndex0 * 2 + (passNumber - 1)) * stepValue;

    //Adding the local progress to the progress

    if (videoJobs.isEmpty())return;

    //Then adds to the progress the relative progress of the current task

    VideoJob currentJob = CurrentVJ();

    float totalVideoDuration =  currentJob.videoInfo.duration; // in seconds
    QString ffmpegOutput = c.ffmpegOutput;
    // frame=  123 fps= 60 q=28.0 size=    1024kB time=00:00:05.12 bitrate=1638.4kbits/s speed=1.00x

    //Checks if it succesfully retrieved the time from the ffmpeg output
    bool gotTime = false;

    float relativeProgress = 0;

    QStringList indexes = ffmpegOutput.split("time=");

    //Gets the time value from the output, ex time=00:00:05.12
    if (ffmpegOutput.startsWith("frame") && indexes.count() >= 2){
        ffmpegOutput =  indexes[1];
        QStringList spaceIndexes = ffmpegOutput.split(" ");
        if (spaceIndexes.count() >= 1){
            ffmpegOutput = spaceIndexes[0];
            gotTime = true;
        }
    }
//Splits every values from time=00:00:05.12 and converts all to seconds
    if (gotTime){
        QStringList parts = ffmpegOutput.split(':');
        if (parts.size() == 3){

            int hour = parts[0].toInt();
            int minutes = parts[1].toInt();
            double seconds = parts[2].toDouble();

            seconds += minutes* 60 + hour * 3600;
            if (seconds > 2){ // to prevent false positives

                //Calculates the relative progress, from the current time of the video and the total duration
                float mult = 100/totalVideoDuration;
                double diff = (seconds*mult) / (totalVideoDuration*mult);

                //Adds the calculated relative progress
                relativeProgress = (diff * 100) / (c.fileCount.toInt() * 2);
            }
        }
    }

    //Adds the relative progress
    int totalProgress = progress + relativeProgress;

    //Prevents progress from going backwards, ok maybe because the calculation is weird and i got fed up from this
    totalProgress = (totalProgress < oldProgress ? oldProgress : totalProgress);
    oldProgress = totalProgress;

    if (abortPressed) totalProgress = 0;

    ui->progressBar->setValue(static_cast<int>(totalProgress));

}


//Starting the compression of all of the selected videos if any
void MainWindow::on_pushButton_compress_pressed()
{
    //Saving the settings of the size thingie
    QSettings settings;
    settings.setValue("sizeLimit",ui->doubleSpinBox_finalSize->value());
    settings.setValue("sizeLimitType",ui->comboBox_finalSizeType->currentIndex());

    //Resets the overriden message of the log
    currentLog.overrideMessage = "";

    //Before compressing, checking if there is any value in the list + if the output folder exists + checks
    if (ui->videoList->count() == 0){
        currentLog.overrideMessage = "No video selected !";
        updateInfo();
        return;
    }

    QDir outputFolder(ui->lineEdit_outputFolder->text());
    if (ui->lineEdit_outputFolder->text() == "" || !outputFolder.exists()){
        currentLog.overrideMessage = "The output folder is invalid !";
        updateInfo();
        return;
    }

    if (ui->doubleSpinBox_finalSize->value() <= 0){
        currentLog.overrideMessage = "You must set a valid fps limit !";
        updateInfo();
        return;
    }

    if (ffmpegPath == "" || ffprobePath == ""){
        currentLog.overrideMessage = "Can't detect a valid ffmpeg or ffprobe instance, check the settings to set them !";
        updateInfo();
        return;
    }

    //Resets dynamic variables
    oldProgress = 0;
    abortPressed = false;
    videoJobs.clear();

    currentLog.overrideMessage = "Preparing videos...";
    updateInfo();

    // Starts by getting every videoinformation using ffprobe
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

    totalVideosCompressing = videoJobs.count();
    currentLog.fileCount = QString::number(videoJobs.count());

    //Starts the Pass1
    job1StartLoop();
}


// Starts or continue the loop of compression for all videos
void MainWindow::job1StartLoop(){

    //This function is called in a loop until no video is left
    if (videoJobs.isEmpty()){
        currentLog.overrideMessage = "Succesfully compressed all of the files !";
        updateInfo();
        ui->progressBar->setValue(100);
        return;
    }

    job2GetVideoData();

}

//Starts by retrieving all video data using ffprobe, such as framerate, duration, width + height, etc
void MainWindow::job2GetVideoData(){

    QProcess *ffprobe = new QProcess(this);

    //this part is called after the original command has been executed
    connect(ffprobe, &QProcess::finished, this, [=](int exitCode, QProcess::ExitStatus){
        qDebug() << "job2 - finished ffprobe with exit code ";
        qDebug() << exitCode;
        if (exitCode != 0) {
            qWarning() << "Error FFMPEG";
            qWarning () << exitCode;
            ffprobe->deleteLater();
            return;
        }

        //Gets the output of the command
        QByteArray output = ffprobe->readAllStandardOutput();
        QJsonDocument doc = QJsonDocument::fromJson(output);

        //Tries to retrieve the data from the generated json
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

        //here we got the full videoInfo set, we start the job3 - Pass1
        job3Pass1();
    });

    //Resets the log
    currentLog.overrideMessage = "";
    currentLog.fileName = QFileInfo(CurrentVJ().inputPath).fileName();
    currentLog.fileIndex = QString::number(totalVideosCompressing - videoJobs.count() + 1);
    currentLog.step = "Retrieving video data";
    currentLog.targetSize = QString::number(ui->doubleSpinBox_finalSize->value() ) + ui->comboBox_finalSizeType->currentText();
    updateInfo();

    //Sets up the ffprobe command and executes it
    QStringList args;
    args << "-v" << "error"
         << "-show_entries"
         << "format=duration:stream=index,codec_type,avg_frame_rate,width,height,bit_rate,sample_rate,channels"
         << "-of" << "json"
         << CurrentVJ().inputPath;

    ffprobe->start(ffprobePath, args);

}

//Starts the Pass1 of the compression, after getting all of the video data
//Pass 1 = Scanning the file and making a file with all of the data
void MainWindow::job3Pass1(){
    currentLog.step = "Pass 1";
    updateInfo();

    QProcess *ffmpegPass1 = new QProcess(this);

    //Called after the original command has been executed and when an update is sent by ffmpeg
    //in order to update the progress bar
    connect(ffmpegPass1, &QProcess::readyReadStandardError, this, [=](){
        if (abortPressed){
            ffmpegPass1->deleteLater();
            updateInfo();
            return;
        }
        currentLog.ffmpegOutput = ffmpegPass1->readAllStandardError();
        updateInfo();
    });

    //Called once the command has been executed and completed, so that it starts the job4 - Pass2
    connect(ffmpegPass1, &QProcess::finished, this, [=](int exitCode, QProcess::ExitStatus){
        qDebug() << "finished ffprobe with exit code";
        qDebug() << exitCode;
        if (exitCode != 0) {
            qWarning() << "Error FFMPEG";
            qWarning() << exitCode;
            ffmpegPass1->deleteLater();
            return;
        }

        job3Pass2();
        //Pass2 !!
    });


    //QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/tempffmpeg/"

    //Making the Pass1
    //Maths to get the final video bitrate + other stuff kms

    //Sets the path for the pass1 log, in the cache folder of cutie
    QString passlogPath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/tempffmpeg/ffmpeg_pass-0.log";
    QDir().mkpath(passlogPath);

    //Pretty sure it can handle any type of number, even for realle huge sizes lol
    long long unsigned targetBitSize;
    long long unsigned videoBitratebps;

    double size = ui->doubleSpinBox_finalSize->value();
    QString type = ui->comboBox_finalSizeType->currentText();

    //Sorry abaienst but not switch for QString ? pretty sure it doesn't change anything once compiled
    //converts the target size to bits
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

    //Calculating the video bitratebps in order to re encode the video with this limit of bits for the video
    //While keeping the audio untouched

    long long unsigned video_bits = targetBitSize - (CurrentVJ().videoInfo.audioBitrateKbps * 1000 * CurrentVJ().videoInfo.duration);
    videoBitratebps = video_bits/CurrentVJ().videoInfo.duration;

    long long unsigned video_bitrate_kbps = std::roundl(videoBitratebps / 1000);
    CurrentVJ().videoInfo.videoBitrateKbps = video_bitrate_kbps;

    double targetFPS = CurrentVJ().videoInfo.fps;

    //if we needs to reencode with custom output fps
    if (ui->spinBox_outputFPS->isEnabled() && ui->spinBox_outputFPS->value() < targetFPS){
        targetFPS = ui->spinBox_outputFPS->value();
        CurrentVJ().videoInfo.fps = targetFPS;
    }

    //Sets up the ffmpeg command and executes it
    QStringList args;
    args << "-y" << "-i" << CurrentVJ().inputPath
         << "-r" << QString::number(targetFPS) << "-c:v" << "libx264"
         << "-b:v" <<  QString::number(video_bitrate_kbps)+"k"
         << "-pass"<<"1"<< "-passlogfile" <<passlogPath << "-an"
         << "-f"<<"null"<< "NUL";

    ffmpegPass1->start(ffmpegPath, args);
}

//job3 - Pass2 !!
//Reencode the video with the calculated bitrate for the video, custom fps if any, and data from the
//generated passlog of ffmpeg
void MainWindow::job3Pass2(){
    currentLog.step = "Pass 2";

    QProcess *ffmpegPass2 = new QProcess(this);

    connect(ffmpegPass2, &QProcess::readyReadStandardError, this, [=](){
        if (abortPressed){
            ffmpegPass2->deleteLater();
            updateInfo();
            return;
        }
        currentLog.ffmpegOutput = ffmpegPass2->readAllStandardError();
        updateInfo();
    });

    connect(ffmpegPass2, &QProcess::finished, this, [=](int exitCode, QProcess::ExitStatus){
        qDebug() << "finished ffmpegPass2 with exit code";
        qDebug() << exitCode;
        if (exitCode != 0) {
            qWarning() << "Error FFMPEG";
            qWarning() << exitCode;
            ffmpegPass2->deleteLater();
            return;
        }
        //Because the compression is done, removes the video from the "queue"
        //just a list lol kill me pardon

        videoJobs.takeFirst();

        //Restarts the loop
        job1StartLoop();
    });


    //Retrieves the generated passlog from ffmpeg to use it in the args
    QString passlogPath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/tempffmpeg/ffmpeg_pass-0.log";

    //Generates the ffmpeg args with the data
    //it took way too much times to do and debug, worst thing ever 10/10

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

//To get the current VideoJob, couldn't correctly gives the same stuct through
//the different functions while changing its data due to skill issue
//TEMP
VideoJob& MainWindow::CurrentVJ(){
    return videoJobs[0];
}

//to abort the whole thing
void MainWindow::on_pushButton_abort_pressed()
{
    abortPressed = true;
    oldProgress = 0;
    ui->progressBar->setValue(0);
}

//To clear the output folder when new files are being compressed
void MainWindow::on_radioButton_clearOutputFolder_pressed()
{
    if (ui->radioButton_clearOutputFolder->isChecked())return;

    QMessageBox msgBox;
    msgBox.setText("By checking this, every mp4 files in the selected directory will be deleted when compressing.");
    msgBox.exec();
    ui->radioButton_clearOutputFolder->setChecked(true);
}


//Opens the output folder if set
void MainWindow::on_pushButton_open_output_folder_pressed()
{
    QSettings settings;
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(settings.value("outputFolder").toString()));
}

//Extends the window to display the dependencies settings, pretty neat i know i know i'm the best
void MainWindow::on_pushButton_open_settings_pressed()
{
    int extendValue = (isExtended ? -200 : 200);
    isExtended = !isExtended;

    resize(windowWidth, windowHeight + extendValue);
    windowHeight += extendValue;

    this->ui->pushButton_open_settings->setText(isExtended ? "Close Dependencies Settings":"Open Dependencies Settings");
}

//To open the dependencies folder
void MainWindow::on_pushButton_open_deps_folder_pressed()
{
    QDir parentFolder(QCoreApplication::applicationDirPath());
    parentFolder.cdUp();

    QDesktopServices::openUrl(
        QUrl::fromLocalFile(parentFolder.absolutePath() + "/deps/"));
}

//Tests if ffmpeg & ffprobe are set, either locally in the deps folder
// or installed globally on the computer via the env var
void MainWindow::refreshDependencies(){

    //Default name for linux of ffmpeg & ffprobe
    QString ffmpegFileName = "ffmpeg";
    QString ffprobeFileName = "ffprobe";

//if win user, adds the extension of the
#ifdef Q_OS_WIN
    ffmpegFileName += ".exe";
    ffprobeFileName += ".exe";
#endif

    //Checking if ffmpeg & ffprobe are detected on the deps folder, if so, try to start them
    QDir parentFolder(QCoreApplication::applicationDirPath());
    parentFolder.cdUp();
    parentFolder.cd("deps");

    bool ffmpegLocal = testProcess(parentFolder.absolutePath() + "/"+ffmpegFileName);
    bool ffprobeLocal = testProcess(parentFolder.absolutePath() + "/"+ffprobeFileName);

    //sets the path for the deps if they are detected
    ffmpegPath = ffmpegLocal ?  parentFolder.absolutePath() + "/"+ffmpegFileName : "";
    ffprobePath = ffprobeLocal ?  parentFolder.absolutePath() + "/"+ffprobeFileName : "";

    bool ffmpegGlobal = false;
    bool ffprobeGlobal = false;

    // if not detected locally, try to load the globally
    if (!ffmpegLocal){
        if (testProcess("ffmpeg")){
            ffmpegGlobal = true;
            ffmpegPath = "ffmpeg";
        }
    }

    if (!ffprobeLocal){
        if (testProcess("ffprobe")){
            ffprobeGlobal = true;
            ffprobePath = "ffprobe";
        }
    }

    //Sets the info value
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

//test the given process, can only be ffmpeg & ffprobe
//Does it sync, even tho it freezes the app, we're sure that it works well
bool MainWindow::testProcess(QString processName){

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

//Refresh dependencies button
void MainWindow::on_pushButton_3_pressed()
{
    refreshDependencies();
}

//To change the theme because sexy
void MainWindow::on_comboBox_theme_currentIndexChanged(int index)
{
    QSettings settings;
    qApp->setStyle(ui->comboBox_theme->currentText());
    settings.setValue("themeIndex",index);
}

