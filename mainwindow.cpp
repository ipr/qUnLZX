#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QStringList>
#include <QFileDialog>
#include <QMessageBox>
#include <QTextEdit>

//#include <QFile>
#include <QDateTime>

#include "UnLzx.h"


MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
	m_szBaseTitle(),
	m_szCurrentArchive(),
	m_PathToItem()
{
    ui->setupUi(this);
	m_szBaseTitle = windowTitle();

	connect(this, SIGNAL(FileSelection(QString)), this, SLOT(onFileSelected(QString)));
	
	QStringList treeHeaders;
	treeHeaders << "Name" 
			<< "Unpacked size" 
			<< "Packed size" 
			<< "Time" 
			<< "Date" 
			<< "Attributes"
			<< "Pack Mode" 
			<< "CRC (A)" 
			<< "CRC (D)" 
			<< "Machine" 
			<< "Comment";
	ui->treeWidget->setColumnCount(treeHeaders.size());
	ui->treeWidget->setHeaderLabels(treeHeaders);
	
	// if file given in command line
	QStringList vCmdLine = QApplication::arguments();
	if (vCmdLine.size() > 1)
	{
		emit FileSelection(vCmdLine[1]);
	}
}

MainWindow::~MainWindow()
{
    delete ui;
}

/*
QString MainWindow::GetPath(const QString &szName)
{
	QString szFilePath = szName;
	szFilePath.replace('\\', "/");
	int iPos = szFilePath.lastIndexOf('/');
	if (iPos == -1)
	{
		// no path
		return szFilePath;
	}
	return szFilePath.left(iPos);
}
*/

bool MainWindow::SplitPathFileName(const QString &szName, QString &szPath, QString &szFile)
{
	QString szFilePath = szName;
	szFilePath.replace('\\', "/");
	int iPos = szFilePath.lastIndexOf('/');
	if (iPos == -1)
	{
		// no path
		return false;
	}

	szFile = szFilePath.right(szFilePath.length() - iPos -1);
	if (szFilePath.at(0) != '/')
	{
		szPath = "/";
	}
	szPath += szFilePath.left(iPos);
	
	// split done
	return true;
}

void MainWindow::ClearAll()
{
	setWindowTitle(m_szBaseTitle);
	
	m_szCurrentArchive.clear();
	m_PathToItem.clear();
	ui->treeWidget->clear();
}

void MainWindow::on_actionArchive_triggered()
{
    // open dialog for user to select file
    QString szFileName = QFileDialog::getOpenFileName(this, tr("Open archive file.."));
    if (szFileName != NULL)
    {
		emit FileSelection(szFileName);
    }
}

void MainWindow::on_actionExtractAll_triggered()
{
	QString szCurrentFilePath = m_szCurrentArchive;
	if (szCurrentFilePath.length() > 0)
	{
		szCurrentFilePath.replace('\\', "/");
		int iPos = szCurrentFilePath.lastIndexOf('/');
		if (iPos != -1)
		{
			// open selection in path of current file?
			szCurrentFilePath = szCurrentFilePath.left(iPos);
		}
		else
		{
			szCurrentFilePath.clear();
		}
	}

    QString szDestPath = QFileDialog::getExistingDirectory(this, tr("Select path to extract to.."), szCurrentFilePath);
    if (szDestPath == NULL)
    {
		return;
    }

	try
	{
		CUnLzx ul(m_szCurrentArchive.toStdString());
		ul.SetExtractPath(szDestPath.toStdString());
		ul.Extract();
		
		QString szOldMessage = ui->statusBar->currentMessage();
		ui->statusBar->showMessage("Extract completed!", 10000);
	}
	catch (std::exception &exp)
	{
		QMessageBox::warning(this, "Error caught",
							 QString::fromLocal8Bit(exp.what()),
							 QMessageBox::Ok);
	}
}

void MainWindow::onFileSelected(QString szArchiveFile)
{
	ClearAll(); // clear previous archive (if any)
	try
	{
		// open given file
		tArchiveEntryList lstArchiveInfo;
		CUnLzx ul(szArchiveFile.toStdString());
		if (ul.View() == true)
		{
			QString szMessage;
			szMessage.append(" Total files in archive: ").append(QString::number(ul.GetTotalFileCount()))
					.append(" Total unpacked size: ").append(QString::number(ul.GetTotalSizeUnpacked()))
					.append(" Archive file size: ").append(QString::number(ul.GetArchiveFileSize()));
			ui->statusBar->showMessage(szMessage);
		}
		else
		{
			ui->statusBar->showMessage(QString("Warning: errors in listing contents"));
		}
		// show what we can
		ul.GetEntryList(lstArchiveInfo);
		
		// show some some info in window title too
		setWindowTitle(m_szBaseTitle + " - " + szArchiveFile);
		m_szCurrentArchive = szArchiveFile;
		
		auto it = lstArchiveInfo.begin();
		auto itEnd = lstArchiveInfo.end();
		while (it != itEnd)
		{
			CArchiveEntry &Entry = (*(it->second));
			
			// skip "merge" instances (if any)
			if (Entry.m_szFileName.length() < 1)
			{
				++it;
				continue;
			}

			QString szPath;
			QString szFile;
			QString szName = QString::fromStdString(Entry.m_szFileName);
			
			if (SplitPathFileName(szName, szPath, szFile) == false)
			{
				// no path
				szFile = szName;
				szPath = "/";
			}
			
			// get top-level item of path
			QTreeWidgetItem *pTopItem = m_PathToItem.value(szPath, nullptr);
			if (pTopItem == nullptr)
			{
				pTopItem = new QTreeWidgetItem((QTreeWidgetItem*)0);
				pTopItem->setText(0, szPath);
				ui->treeWidget->addTopLevelItem(pTopItem);
				m_PathToItem.insert(szPath, pTopItem);
			}

			// add sub-item of file
			QTreeWidgetItem *pSubItem = new QTreeWidgetItem(pTopItem);
			pSubItem->setText(0, szFile);
			pSubItem->setText(1, QString::number(Entry.m_ulUnpackedSize)); // always given
			if (Entry.m_bPackedSizeAvailable == true) // not merged
			{
				pSubItem->setText(2, QString::number(Entry.m_ulPackedSize)); // always given
			}
			else
			{
				// merged? (no packed-size available)
				//pSubItem->setText(2, "n/a");
				pSubItem->setText(2, "(Merged)");
			}
			
			/*
			QDateTime Stamp;
			Stamp.setDate(QDate(year, month, day));
			Stamp.setTime(QTime(hour, minute, second));
			*/

			QString szTime;
			szTime.sprintf("%02ld:%02ld:%02ld", 
						   Entry.m_Timestamp.hour, 
						   Entry.m_Timestamp.minute, 
						   Entry.m_Timestamp.second);
			pSubItem->setText(3, szTime);
			//pSubItem->setText(3, QTime(hour, minute, second).toString());

			QString szDate;
			szDate.sprintf("%.2ld-%.2ld-%4ld", 
						   Entry.m_Timestamp.day, 
						   Entry.m_Timestamp.month, 
						   Entry.m_Timestamp.year);
			pSubItem->setText(4, szDate);
			//pSubItem->setText(4, QDate(year, month, day).toString());

			// file-attributes (Amiga-style: HSPA RWED)
			QString szAttribs;
			szAttribs.sprintf("%c%c%c%c%c%c%c%c", 
							  (Entry.m_Attributes.h) ? 'h' : '-',
							  (Entry.m_Attributes.s) ? 's' : '-',
							  (Entry.m_Attributes.p) ? 'p' : '-',
							  (Entry.m_Attributes.a) ? 'a' : '-',
							  (Entry.m_Attributes.r) ? 'r' : '-',
							  (Entry.m_Attributes.w) ? 'w' : '-',
							  (Entry.m_Attributes.e) ? 'e' : '-',
							  (Entry.m_Attributes.d) ? 'd' : '-');
			pSubItem->setText(5, szAttribs);
			
			// packing mode
			pSubItem->setText(6, QString::number((int)Entry.m_PackMode));
			
			QString szCrcA; // CRC of entry in archive
			szCrcA.sprintf("%x", Entry.m_uiCrc);
			pSubItem->setText(7, szCrcA);

			QString szCrcD; // CRC of data
			szCrcD.sprintf("%x", Entry.m_uiDataCrc);
			pSubItem->setText(8, szCrcD);

			// machine-type enum
			pSubItem->setText(9, QString::number((int)Entry.m_MachineType));
			
			// file-related comment (if any stored)
			pSubItem->setText(10, QString::fromStdString(Entry.m_szComment));
			
			pTopItem->addChild(pSubItem);
			
			++it;
		}
		
		ui->treeWidget->expandAll();
		ui->treeWidget->resizeColumnToContents(0);
		ui->treeWidget->sortByColumn(0);
	}
	catch (std::exception &exp)
	{
		QMessageBox::warning(this, "Error caught",
							 QString::fromLocal8Bit(exp.what()),
							 QMessageBox::Ok);
	}
}


void MainWindow::on_actionAbout_triggered()
{
	QTextEdit *pTxt = new QTextEdit(this);
	pTxt->setWindowFlags(Qt::Window); //or Qt::Tool, Qt::Dialog if you like
	pTxt->setReadOnly(true);
	pTxt->append("qUnLzx by Ilkka Prusi 2011");
	pTxt->append("");
	pTxt->append("This program is free to use and distribute. No warranties of any kind.");
	pTxt->append("Program uses Qt 4.7.1 under LGPL v. 2.1");
	pTxt->append("");
	pTxt->append("Keyboard shortcuts:");
	pTxt->append("");
	pTxt->append("F = open file (LZX archive)");
	pTxt->append("Esc = close");
	pTxt->append("? = about (this dialog)");
	pTxt->append("");
	pTxt->show();
}

