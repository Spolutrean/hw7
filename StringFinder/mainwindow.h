#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMutex>
#include <QFutureWatcher>
#include <QFileSystemWatcher>

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    QVector<QVector<int> > distributedFiles;
    quint8 threadsCount;

    static std::vector<std::pair<QString, int> > allThreeGrams;
    static quint64 sizeOfAllFiles, buildingProgress, findingProgress;
    static QVector<std::pair<quint64, QString> > foundedFiles;
    static QString searchString;
    static QVector<std::pair<int, QVector<quint64> > > foundedStrings;
    static bool stopBuilding, stopFinding, indexingInProgress;
    static QFileSystemWatcher *fileWatcher;

    static QMutex mutex;
    static QFutureWatcher<void> indexingWatcher, threeGramBuildingWatcher, stringFindWatcher, sortingThreeGramsWatcher;
    static void buildThreeGramOnBlock(const QVector<int>& filesIndexesInBlock);
    static void indexFilesInDirectory(QString const &curDirPath);
    static void findStringOnBlock(QVector<int>& filesIndexesInBlock);
    static void sortAllThreeGrams();

    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

    void distributeFilesEvenly(QVector<int> &files, QVector<QVector<int> > &result);
    void startBuildingThreeGram();
    void buildTree();

    QVector<int> filesWithThisThreeGram(const QString &tgm);


private slots:
    void on_selectDirButton_clicked();
    void on_startIndexingButton_clicked();
    void filesWereIndexed();
    void on_startFindingButton_clicked();
    void needStopBuilding();
    void needStopFinding();
    void somethingChanged(const QString &path);
    void threeGramsWereBuilt();

private:
    Ui::MainWindow *ui;
};

#endif // MAINWINDOW_H
