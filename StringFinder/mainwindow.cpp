#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QFileDialog>
#include <QVector>
#include <QProgressDialog>
#include <set>
#include <QTreeWidgetItem>
#include <QMessageBox>

#include "QtConcurrent/qtconcurrentrun.h"
#include "QtConcurrent/qtconcurrentmap.h"

QMutex MainWindow::mutex;
QFutureWatcher<void> MainWindow::indexingWatcher, MainWindow::threeGramBuildingWatcher, MainWindow::stringFindWatcher, MainWindow::sortingThreeGramsWatcher;
QVector<std::pair<quint64, QString> > MainWindow::foundedFiles;
std::vector<std::pair<QString, int> > MainWindow::allThreeGrams;
quint64 MainWindow::sizeOfAllFiles, MainWindow::buildingProgress, MainWindow::findingProgress;
QString MainWindow::searchString;
QVector<std::pair<int, QVector<quint64> > > MainWindow::foundedStrings;
bool MainWindow::stopBuilding, MainWindow::stopFinding, MainWindow::indexingInProgress;
QFileSystemWatcher* MainWindow::fileWatcher;

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    fileWatcher = new QFileSystemWatcher(this);
    connect(&indexingWatcher, SIGNAL(finished()), this, SLOT(filesWereIndexed()));
    ui->threadsCount->setValue(QThread::idealThreadCount());
    ui->treeWidget->setHeaderLabel("Founded string in files:");
    connect(fileWatcher, SIGNAL(fileChanged(QString)), this, SLOT(somethingChanged(QString)));
    connect(fileWatcher, SIGNAL(directoryChanged(QString)), this, SLOT(somethingChanged(QString)));
    connect(&sortingThreeGramsWatcher, SIGNAL(finished()), this, SLOT(threeGramsWereBuilt()));

    /*
    QByteArray ar(QString::fromUtf8("🦖").toUtf8());
    qDebug() << QString::number(ar.size());
    QByteArray first(2, '2');
    first[0] = ar[0];
    first[1] = ar[1];
    QByteArray second(2, '2');
    second[0] = ar[2];
    second[1] = ar[3];
    QString firstChar = QString::fromStdString(first.toStdString());
    QString secondChar = QString::fromStdString(second.toStdString());
    QChar c00 = firstChar[0], c01 = firstChar[1];
    QChar c10 = secondChar[0], c11 = secondChar[1];*/
}

MainWindow::~MainWindow()
{
    delete ui;
}


void MainWindow::needStopBuilding()
{
    stopBuilding = true;
}

void MainWindow::needStopFinding()
{
    stopFinding = true;
}

void MainWindow::somethingChanged(const QString &path)
{
    on_startIndexingButton_clicked();
}

void MainWindow::on_selectDirButton_clicked()
{
    QString dir = QFileDialog::getExistingDirectory(this,
                                                    "Select Directory for scanning",
                                                    QString(),
                                                    QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    ui->dirPath->setText(dir);
    ui->startIndexingButton->setEnabled(true);
}

void MainWindow::indexFilesInDirectory(QString const &dirPath)
{
    QDir curDir(dirPath);
    if(!curDir.isReadable() || !curDir.exists())
    {
        return;
    }
    fileWatcher->addPath(dirPath);
    QStringList files = curDir.entryList(QDir::Files | QDir::NoSymLinks);
    QStringList dirs = curDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    for(QString &fileName : files)
    {
        QString filePath(dirPath + "/" + fileName);
        fileWatcher->addPath(filePath);
        QFileInfo fileInfo(filePath);
        if(fileInfo.isReadable())
        {
            foundedFiles.push_back({fileInfo.size(), filePath});
            sizeOfAllFiles += (fileInfo.size() >> 15);
        }
    }
    for(QString &dir : dirs)
    {
        indexFilesInDirectory(dirPath + "/" + dir);
    }
}

void MainWindow::on_startIndexingButton_clicked()
{
    if(indexingInProgress) {
        return;
    }

    ui->selectDirButton->setEnabled(false);
    ui->threadsCount->setEnabled(false);
    ui->startIndexingButton->setEnabled(false);
    threadsCount = ui->threadsCount->value();
    sizeOfAllFiles = 0;
    foundedFiles.clear();

    indexingInProgress = true;
    indexingWatcher.setFuture(QtConcurrent::run(&MainWindow::indexFilesInDirectory, ui->dirPath->text()));
    ui->statusBar->showMessage("Indexing files in directory...");
}

void MainWindow::distributeFilesEvenly(QVector<int> &files, QVector<QVector<int> > &result)
{
    result.clear();
    result.resize(threadsCount);
    //sort with size of files
    std::sort(files.begin(), files.end(), [](const quint64 & a, const quint64 & b){
        return foundedFiles[a].first < foundedFiles[b].first;
    });

    std::set<std::pair<quint64, quint8> > minCompletedThread;
    for(size_t i = 0; i < threadsCount; ++i)
    {
        minCompletedThread.insert({0, i});
    }
    for(size_t i = 0; i < files.size(); ++i)
    {
        std::pair<quint64, QString> file = foundedFiles[files[i]];
        auto it = minCompletedThread.begin();
        std::pair<quint64, quint8> curThread = *it;
        minCompletedThread.erase(it);
        result[curThread.second].push_back(files[i]);
        minCompletedThread.insert({curThread.first + file.first, curThread.second});
    }
}

void MainWindow::filesWereIndexed()
{
    indexingInProgress = false;
    ui->statusBar->showMessage("Files were indexed.");
    threadsCount  = qMin((int)threadsCount, foundedFiles.size());
    QVector<int> indexesOfAllFiles(foundedFiles.size());
    std::iota(indexesOfAllFiles.begin(), indexesOfAllFiles.end(), 0);
    distributeFilesEvenly(indexesOfAllFiles, distributedFiles);

    startBuildingThreeGram();
}

void MainWindow::buildThreeGramOnBlock(const QVector<int> &filesIndexesInBlock)
{
    QVector<std::pair<QString, int> > threeGramsInBlock;
    for(int fileNum = 0; fileNum < filesIndexesInBlock.size(); ++fileNum)
    {
        if(MainWindow::stopBuilding) {
            return;
        }
        QString filePath = foundedFiles[filesIndexesInBlock[fileNum]].second;
        std::set<QString> threeGramsInFile;
        QFile file(filePath);
        if(!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        QTextStream inputStream(&file);
        bool ok = true;

        QChar first, second, third, curChar;

        while(!inputStream.atEnd()) {
            if(MainWindow::stopBuilding) {
                file.close();
                return;
            }
            inputStream >> curChar;

            if(inputStream.status() != QTextStream::Ok || curChar.isNonCharacter()) {
                ok = false;
                break;
            }

            //shift chars
            first = second;
            second = third;
            third = curChar;

            if(!first.isNull()) {
                QString tgm = "";
                tgm += first;
                tgm += second;
                tgm += third;
                threeGramsInFile.insert(tgm);
            }
        }
        file.close();

        if(MainWindow::stopBuilding) {
            return;
        }

        //update progress
        mutex.lock();
        buildingProgress += (file.size() >> 15);
        emit MainWindow::threeGramBuildingWatcher.progressValueChanged(buildingProgress);
        mutex.unlock();

        if(ok) {
            //add three grams in file to block three grams
            for(const QString &tgm : threeGramsInFile) {
                threeGramsInBlock.push_back({tgm, filesIndexesInBlock[fileNum]});
            }
        }
    }
    if(MainWindow::stopBuilding) {
        return;
    }
    qDebug() << "thread started merge information";
    mutex.lock();
    std::move(threeGramsInBlock.begin(), threeGramsInBlock.end(), std::back_inserter(allThreeGrams));
    mutex.unlock();
    qDebug() << "thread finished merge information";
}

void MainWindow::sortAllThreeGrams()
{
    std::sort(allThreeGrams.begin(), allThreeGrams.end());
}

void MainWindow::startBuildingThreeGram()
{
    ui->statusBar->showMessage("3-gram building...");
    buildingProgress = 0;
    allThreeGrams.clear();

    stopBuilding = false;
    QProgressDialog pdialog;
    pdialog.setLabelText("Building a three grams...");
    pdialog.setRange(0, sizeOfAllFiles);
    connect(&pdialog, SIGNAL(canceled()), &threeGramBuildingWatcher, SLOT(cancel()));
    connect(&pdialog, SIGNAL(canceled()), this, SLOT(needStopBuilding()));
    connect(&threeGramBuildingWatcher, SIGNAL(finished()), &pdialog, SLOT(reset()));
    connect(&threeGramBuildingWatcher, SIGNAL(progressValueChanged(int)), &pdialog, SLOT(setValue(int)));

    threeGramBuildingWatcher.setFuture(QtConcurrent::map(distributedFiles, &MainWindow::buildThreeGramOnBlock));

    pdialog.exec();
    threeGramBuildingWatcher.waitForFinished();

    if(threeGramBuildingWatcher.isCanceled())
    {
        QMessageBox::critical(this, "Canceled", "Building has been canceled.");
        ui->startIndexingButton->setEnabled(true);
        ui->selectDirButton->setEnabled(true);
        ui->threadsCount->setEnabled(true);

        ui->statusBar->showMessage("Hashing has been canceled.");
    }
    else
    {
        ui->statusBar->showMessage("Sorting threegrams...");
        sortingThreeGramsWatcher.setFuture(QtConcurrent::run(&MainWindow::sortAllThreeGrams));
    }

}

void MainWindow::threeGramsWereBuilt()
{
    ui->statusBar->showMessage("3-grams were built.");
    ui->startFindingButton->setEnabled(true);
    ui->threadsCount->setEnabled(true);
    ui->selectDirButton->setEnabled(true);
}

void MainWindow::buildTree()
{
    for(std::pair<int, QVector<quint64> > &file : foundedStrings) {
        QTreeWidgetItem *rootItem = new QTreeWidgetItem(ui->treeWidget);
        QString headerText = foundedFiles[file.first].second;
        headerText.remove(0, ui->dirPath->text().size());
        rootItem->setText(0, headerText + " x" + QString::number(file.second.size()));
        for(quint64 &pos : file.second) {
            QTreeWidgetItem *childItem = new QTreeWidgetItem();
            childItem->setText(0, QString::number(pos));
            rootItem->addChild(childItem);
        }
        ui->treeWidget->addTopLevelItem(rootItem);
    }
}

QVector<int> MainWindow::filesWithThisThreeGram(const QString &tgm)
{
    QVector<int> files;
    auto it = std::lower_bound(allThreeGrams.begin(), allThreeGrams.end(), std::make_pair(tgm, 0));
    while(it != allThreeGrams.end() && (*it).first == tgm) {
        files.push_back((*it).second);
        ++it;
    }

    return files;
}

void MainWindow::findStringOnBlock(QVector<int> &filesIndexesInBlock)
{
    QVector<std::pair<int, QVector<quint64> > > foundedStringsOnBlock;
    for(int& fileNum : filesIndexesInBlock) {

        if(MainWindow::stopFinding) {
            return;

        }
        QVector<quint64> stringPositions;

        mutex.lock();
        QFile file(foundedFiles[fileNum].second);
        mutex.unlock();

        if(!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        QTextStream inputStream(&file);

        QString tmp;
        quint64 pos = 0;
        while(!inputStream.atEnd() && tmp.size() != searchString.size()) {
            QChar curChar;
            inputStream >> curChar;
            ++pos;
            tmp += curChar;
            if(tmp == searchString) {
                stringPositions.push_back(pos - searchString.size());
            }
        }
        while(!inputStream.atEnd()) {
            if(MainWindow::stopFinding) {
                file.close();
                return;
            }
            QChar curChar;
            inputStream >> curChar;
            ++pos;

            //shift
            for(int i = 0; i < tmp.size() - 1; ++i) {
                tmp[i] = tmp[i + 1];
            }

            tmp[tmp.size() - 1] = curChar;

            if(tmp == searchString) {
                stringPositions.push_back(pos - searchString.size());
            }
        }

        if(stringPositions.size()) {
            foundedStringsOnBlock.push_back({fileNum, stringPositions});
        }

        file.close();

        //update info
        mutex.lock();
        findingProgress += (file.size() >> 15);
        emit MainWindow::stringFindWatcher.progressValueChanged(findingProgress);
        mutex.unlock();
    }

    if(MainWindow::stopFinding) {
        return;
    }
    mutex.lock();
    std::move(foundedStringsOnBlock.begin(), foundedStringsOnBlock.end(), std::back_inserter(foundedStrings));
    mutex.unlock();
}

void MainWindow::on_startFindingButton_clicked()
{
    ui->treeWidget->clear();
    foundedStrings.clear();
    distributedFiles.clear();
    searchString = ui->textForSearch->text();

    std::set<QString> threeGramsInText;
    QChar first, second, third, curChar;
    for(int i = 0; i < searchString.size(); ++i) {
        curChar = searchString[i];
        first = second;
        second = third;
        third = curChar;
        if(!first.isNull()) {
            QString s;
            s += first;
            s += second;
            s += third;
            threeGramsInText.insert(s);
        }
    }


    QMap<int, int> usedCount;
    QVector<int> filesWithAllThreeGrams;
    int maxUsedCnt = -1, handledThreeGramCount = 0;
    bool ok = true;
    if(searchString.size() >= 3)
    {
        for(auto it = threeGramsInText.begin(); it != threeGramsInText.end(); ++handledThreeGramCount, ++it) {
            QString threeGram = *it;
            QVector<int> files = filesWithThisThreeGram(threeGram);
            for(int &fileNum : files) {
                maxUsedCnt = qMax(maxUsedCnt, usedCount[fileNum]++);
            }
            if(maxUsedCnt != handledThreeGramCount) {
                ok = false;
                break;
            }
            //if handled last threegram, then select files with all threegrams
            if(handledThreeGramCount == threeGramsInText.size() - 1) {
                for(int &fileNum : files) {
                    if(usedCount[fileNum] == maxUsedCnt + 1) {
                        filesWithAllThreeGrams.push_back(fileNum);
                    }
                }
            }
        }
    } else {
        for(std::pair<QString, int> & treeGram : allThreeGrams) {
            if(treeGram.first.contains(searchString)) {
                filesWithAllThreeGrams.push_back(treeGram.second);
            }
        }
        std::sort(filesWithAllThreeGrams.begin(), filesWithAllThreeGrams.end());
        filesWithAllThreeGrams.resize(std::unique(filesWithAllThreeGrams.begin(), filesWithAllThreeGrams.end()) - filesWithAllThreeGrams.begin());
    }

    if(filesWithAllThreeGrams.size() == 0) {
        ok = false;
    }

    if (ok) {
        threadsCount = qMin(ui->threadsCount->value(), filesWithAllThreeGrams.size());
        distributeFilesEvenly(filesWithAllThreeGrams, distributedFiles);

        findingProgress = 0;
        stopFinding = false;
        QProgressDialog pdialog;
        pdialog.setLabelText("Finding a string...");
        quint64 sizeOfFilesWithThreeGrams = 0;
        for(int fileId : filesWithAllThreeGrams) {
            sizeOfFilesWithThreeGrams += foundedFiles[fileId].first;
        }
        pdialog.setRange(0, sizeOfFilesWithThreeGrams>>15);
        connect(&pdialog, SIGNAL(canceled()), &stringFindWatcher, SLOT(cancel()));
        connect(&pdialog, SIGNAL(canceled()), this, SLOT(needStopFinding()));
        connect(&stringFindWatcher, SIGNAL(finished()), &pdialog, SLOT(reset()));
        connect(&stringFindWatcher, SIGNAL(progressValueChanged(int)), &pdialog, SLOT(setValue(int)));

        stringFindWatcher.setFuture(QtConcurrent::map(distributedFiles, &MainWindow::findStringOnBlock));

        pdialog.exec();
        stringFindWatcher.waitForFinished();

        if(stringFindWatcher.isCanceled()) {
            QMessageBox::critical(this, "Canceled", "Finding has been canceled.");
            ui->startIndexingButton->setEnabled(true);
            ui->selectDirButton->setEnabled(true);
            ui->threadsCount->setEnabled(true);
            ui->statusBar->showMessage("Finding has been canceled.");
        } else {
            buildTree();
        }
    } else {
        ui->statusBar->showMessage("String wasn't found");
    }
}
