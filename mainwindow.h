#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "videojob.h"
#include "QListWidgetItem"
QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:

    void on_button_AddVideos_pressed();

    void on_button_removeSelectedVideo_pressed();

    void on_toolButton_choseOutputFolder_pressed();

    void on_checkBox_outputFPS_pressed();

    void on_pushButton_compress_pressed();

    void updateInfo();

    void job1StartLoop();

    void job2GetVideoData();

    void job3Pass1();

    void job3Pass2();

    VideoJob& CurrentVJ();


    void on_pushButton_abort_pressed();

    void on_radioButton_clearOutputFolder_pressed();

    void on_pushButton_open_output_folder_pressed();

    void on_pushButton_open_settings_pressed();

    void on_pushButton_open_deps_folder_pressed();

    void refreshDependencies();

    bool testProcess(QString processName);

    void on_pushButton_3_pressed();

    void on_comboBox_theme_currentIndexChanged(int index);

private:
    Ui::MainWindow *ui;
};
#endif // MAINWINDOW_H
