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
			CArchiveEntry *pEntry = it->second;

			/*
			// skip "merge" instances (if any)
			if (pEntry->m_szFileName.length() < 1)
			{
				// test
				//QTreeWidgetItem *pTopItem = new QTreeWidgetItem((QTreeWidgetItem*)0);
				//pTopItem->setText(0, "(Merged file)");
				
				++it;
				continue;
			}
			*/

			/*
			// empty "store-only" file?
			// -> some cases have this for directory-entry..
			if (pEntry->m_ulUnpackedSize == 0
				&& pEntry->m_PackMode == tLzxArchiveHeader::HDR_PACK_STORE)
			{
				++it;
				continue;
			}
			*/

			QString szPath;
			QString szFile;
			QString szName = QString::fromStdString(pEntry->m_szFileName);
			
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
			
			/*
			// experimental, show merged-files as groups in display
			if (pEntry->m_pGroup != nullptr)
			{
				QTreeWidgetItem *pGroupItem = m_GroupToItem.value(pEntry->m_pGroup, nullptr);
				if (pGroupItem == nullptr)
				{
					pGroupItem = new QTreeWidgetItem(pTopItem);
					pTopItem->addChild(pGroupItem);
					m_GroupToItem.insert(pEntry->m_pGroup, pGroupItem);
				}
				pGroupItem->setText(0, "(Merged group)");
				pGroupItem->setText(1, QString::number(pEntry->m_pGroup->m_ulGroupUnpackedSize)); 
				pGroupItem->setText(2, QString::number(pEntry->m_pGroup->m_ulGroupPackedSize)); 
				
				// add sub-item of file
				QTreeWidgetItem *pSubItem = new QTreeWidgetItem(pGroupItem);
				
				EntryToViewItem(szFile, pEntry, pSubItem);
				pGroupItem->addChild(pSubItem);
				m_EntryToItem.insert(pEntry, pSubItem);
			}
			else
			{
				// add sub-item of file
				QTreeWidgetItem *pSubItem = new QTreeWidgetItem(pTopItem);
				
				EntryToViewItem(szFile, pEntry, pSubItem);
				pTopItem->addChild(pSubItem);
				m_EntryToItem.insert(pEntry, pSubItem);
			}
			*/
			
			// add sub-item of file
			QTreeWidgetItem *pSubItem = new QTreeWidgetItem(pTopItem);
			
			EntryToViewItem(szFile, pEntry, pSubItem);
			pTopItem->addChild(pSubItem);
			m_EntryToItem.insert(pEntry, pSubItem);
			
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

void MainWindow::EntryToViewItem(QString &szFile, CArchiveEntry *pEntry, QTreeWidgetItem *pSubItem)
{
	pSubItem->setText(0, szFile);
	pSubItem->setText(1, QString::number(pEntry->m_ulUnpackedSize)); // always given
	if (pEntry->m_bPackedSizeAvailable == true) // not merged
	{
		pSubItem->setText(2, QString::number(pEntry->m_ulPackedSize)); // always given
	}
	else
	{
		// merged? (no packed-size available)
		//pSubItem->setText(2, "n/a");
		pSubItem->setText(2, "(Merged)");
	}
	
	
	QString szTime;
	szTime.sprintf("%02ld:%02ld:%02ld", 
				   pEntry->m_Timestamp.hour, 
				   pEntry->m_Timestamp.minute, 
				   pEntry->m_Timestamp.second);
	pSubItem->setText(3, szTime);
	
	QString szDate;
	szDate.sprintf("%.2ld-%.2ld-%4ld", 
				   pEntry->m_Timestamp.day, 
				   pEntry->m_Timestamp.month, 
				   pEntry->m_Timestamp.year);
	pSubItem->setText(4, szDate);
	
	// file-attributes (Amiga-style: HSPA RWED)
	QString szAttribs;
	szAttribs.sprintf("%c%c%c%c%c%c%c%c", 
					  (pEntry->m_Attributes.h) ? 'h' : '-',
					  (pEntry->m_Attributes.s) ? 's' : '-',
					  (pEntry->m_Attributes.p) ? 'p' : '-',
					  (pEntry->m_Attributes.a) ? 'a' : '-',
					  (pEntry->m_Attributes.r) ? 'r' : '-',
					  (pEntry->m_Attributes.w) ? 'w' : '-',
					  (pEntry->m_Attributes.e) ? 'e' : '-',
					  (pEntry->m_Attributes.d) ? 'd' : '-');
	pSubItem->setText(5, szAttribs);
	
	// packing mode
	pSubItem->setText(6, QString::number((int)pEntry->m_PackMode));
	
	QString szCrcA; // CRC of entry in archive
	szCrcA.sprintf("%x", pEntry->m_uiCrc);
	pSubItem->setText(7, szCrcA);
	
	QString szCrcD; // CRC of data
	szCrcD.sprintf("%x", pEntry->m_uiDataCrc);
	pSubItem->setText(8, szCrcD);
	
	// machine-type enum
	pSubItem->setText(9, QString::number((int)pEntry->m_MachineType));
	
	// file-related comment (if any stored)
	pSubItem->setText(10, QString::fromStdString(pEntry->m_szComment));
	
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

