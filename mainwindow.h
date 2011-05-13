#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMap>
#include <QTreeWidgetItem>


namespace Ui {
    class MainWindow;
}

class CMergeGroup;
class CArchiveEntry;


class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();
	
signals:
	void FileSelection(QString szFile);

private:
    Ui::MainWindow *ui;
	QString m_szBaseTitle;
	QString m_szCurrentArchive;
	
	// key: path (without filename)
	// value: top-level item
	//
	QMap<QString, QTreeWidgetItem*> m_PathToItem;
	
	// for merge-group display
	QMap<CMergeGroup*, QTreeWidgetItem*> m_GroupToItem;
	QMap<CArchiveEntry*, QTreeWidgetItem*> m_EntryToItem;
	
	//QString GetPath(const QString &szName);
	bool SplitPathFileName(const QString &szName, QString &szPath, QString &szFile);
	
	void ClearAll();

	void EntryToViewItem(QString &szFile, CArchiveEntry *pEntry, QTreeWidgetItem *pSubItem);
	

private slots:
    void on_actionExtractAll_triggered();
    void on_actionAbout_triggered();
    void on_actionArchive_triggered();
	
	void onFileSelected(QString szArchiveFile);
};

#endif // MAINWINDOW_H
