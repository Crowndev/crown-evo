#include <qt/systemnodelist.h>
#include <qt/forms/ui_systemnodelist.h>

#include <crown/legacysigner.h>
#include <crown/init.h>
#include <init.h>
#include <key_io.h>
#include <systemnode/activesystemnode.h>
#include <systemnode/systemnode-sync.h>
#include <systemnode/systemnodeconfig.h>
#include <systemnode/systemnodeman.h>
#include <node/context.h>
#include <rpc/blockchain.h>
#include <qt/addresstablemodel.h>
#include <qt/bitcoinunits.h>
#include <qt/clientmodel.h>
#include <qt/createsystemnodedialog.h>
#include <qt/datetablewidgetitem.h>
#include <qt/guiutil.h>
#include <qt/privatekeywidget.h>
#include <qt/optionsmodel.h>
#include <qt/startmissingdialog.h>
#include <qt/walletmodel.h>
#include <sync.h>
#include <util/time.h>
#include <wallet/wallet.h>

#include <QMessageBox>
#include <QTimer>

RecursiveMutex cs_systemnodes;

SystemnodeList::SystemnodeList(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SystemnodeList),
    clientModel(0),
    walletModel(0)
{
    ui->setupUi(this);

    ui->startButton->setEnabled(false);

    int columnAliasWidth = 100;
    int columnAddressWidth = 200;
    int columnProtocolWidth = 60;
    int columnStatusWidth = 80;
    int columnActiveWidth = 130;
    int columnLastSeenWidth = 130;

    ui->tableWidgetMySystemnodes->setColumnWidth(0, columnAliasWidth);
    ui->tableWidgetMySystemnodes->setColumnWidth(1, columnAddressWidth);
    ui->tableWidgetMySystemnodes->setColumnWidth(2, columnProtocolWidth);
    ui->tableWidgetMySystemnodes->setColumnWidth(3, columnStatusWidth);
    ui->tableWidgetMySystemnodes->setColumnWidth(4, columnActiveWidth);
    ui->tableWidgetMySystemnodes->setColumnWidth(5, columnLastSeenWidth);

    ui->tableWidgetSystemnodes->setColumnWidth(0, columnAddressWidth);
    ui->tableWidgetSystemnodes->setColumnWidth(1, columnProtocolWidth);
    ui->tableWidgetSystemnodes->setColumnWidth(2, columnStatusWidth);
    ui->tableWidgetSystemnodes->setColumnWidth(3, columnActiveWidth);
    ui->tableWidgetSystemnodes->setColumnWidth(4, columnLastSeenWidth);

    ui->tableWidgetMySystemnodes->setContextMenuPolicy(Qt::CustomContextMenu);

    QAction* startAliasAction = new QAction(tr("Start alias"), this);
    contextMenu = new QMenu();
    contextMenu->addAction(startAliasAction);
    connect(ui->tableWidgetMySystemnodes, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(showContextMenu(const QPoint&)));
    connect(startAliasAction, SIGNAL(triggered()), this, SLOT(on_startButton_clicked()));
    connect(ui->reloadButton, SIGNAL(triggered()), this, SLOT(on_reloadButton_clicked()));

    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(updateNodeList()));
    connect(timer, SIGNAL(timeout()), this, SLOT(updateMyNodeList()));
    timer->start(1000);

    updateNodeList();

    // Fill MN list
    fFilterUpdated = false;
    nTimeFilterUpdated = GetTime();
    updateNodeList();
}

SystemnodeList::~SystemnodeList()
{
    delete ui;
}

void SystemnodeList::notReady()
{
     QMessageBox::critical(this, tr("Command is not available right now"),
         tr("You can't use this command until systemnode list is synced"));
     return;
}

void SystemnodeList::setClientModel(ClientModel *model)
{
    if (this->clientModel == NULL)
    {
        this->clientModel = model;
        if(model)
        {
            // try to update list when systemnode count changes
            connect(clientModel, SIGNAL(strSystemnodesChanged(QString)), this, SLOT(updateNodeList()));
        }
    }
}

void SystemnodeList::setWalletModel(WalletModel *model)
{
    if (this->walletModel == NULL)
    {
        this->walletModel = model;
    }
}

void SystemnodeList::showContextMenu(const QPoint& point)
{
    QTableWidgetItem* item = ui->tableWidgetMySystemnodes->itemAt(point);
    if (item) contextMenu->exec(QCursor::pos());
}

void SystemnodeList::StartAlias(std::string strAlias)
{
    std::string strStatusHtml;
    strStatusHtml += "<center>Alias: " + strAlias;

    for (CNodeEntry sne : systemnodeConfig.getEntries()) {
        if (sne.getAlias() == strAlias) {
            std::string strError;
            CSystemnodeBroadcast snb;

            bool fSuccess = CSystemnodeBroadcast::Create(sne.getIp(), sne.getPrivKey(), sne.getTxHash(), sne.getOutputIndex(), strError, snb);

            if (fSuccess) {
                strStatusHtml += "<br>Successfully started systemnode.";
                snodeman.UpdateSystemnodeList(snb, *g_rpc_node->connman);
                snb.Relay(*g_rpc_node->connman);
            } else {
                strStatusHtml += "<br>Failed to start systemnode.<br>Error: " + strError;
            }
            break;
        }
    }
    strStatusHtml += "</center>";

    QMessageBox msg;
    msg.setText(QString::fromStdString(strStatusHtml));
    msg.exec();

    updateMyNodeList(true);
}

void SystemnodeList::StartAll(std::string strCommand)
{
    int nTotal = 0;
    int nCountSuccessful = 0;
    int nCountFailed = 0;
    std::string strFailedHtml;

    for (CNodeEntry sne : systemnodeConfig.getEntries()) {
        std::string strError;
        CSystemnodeBroadcast snb;

        CTxIn txin = CTxIn(uint256S(sne.getTxHash()), uint32_t(atoi(sne.getOutputIndex().c_str())));
        CSystemnode* pmn = snodeman.Find(txin);

        if (strCommand == "start-missing" && pmn) continue;

        bool fSuccess = CSystemnodeBroadcast::Create(sne.getIp(), sne.getPrivKey(), sne.getTxHash(), sne.getOutputIndex(), strError, snb);

        if (fSuccess) {
            nCountSuccessful++;
            snodeman.UpdateSystemnodeList(snb, *g_rpc_node->connman);
            snb.Relay(*g_rpc_node->connman);
        } else {
            nCountFailed++;
            strFailedHtml += "\nFailed to start " + sne.getAlias() + ". Error: " + strError;
        }
        nTotal++;
    }
    GetMainWallet()->Lock();

    std::string returnObj;
    returnObj = strprintf("Successfully started %d systemnodes, failed to start %d, total %d", nCountSuccessful, nCountFailed, nCountFailed + nCountSuccessful);
    if (nCountFailed > 0) {
        returnObj += strFailedHtml;
    }

    QMessageBox msg;
    msg.setText(QString::fromStdString(returnObj));
    msg.exec();

    updateMyNodeList(true);
}

void SystemnodeList::updateMySystemnodeInfo(QString strAlias, QString strAddr, QString privkey, QString txHash, QString txIndex, CSystemnode *pmn)
{
    LOCK(cs_snlistupdate);
    bool fOldRowFound = false;
    int nNewRow = 0;

    for (int i = 0; i < ui->tableWidgetMySystemnodes->rowCount(); i++) {
        if (ui->tableWidgetMySystemnodes->item(i, 0)->text() == strAlias) {
            fOldRowFound = true;
            nNewRow = i;
            break;
        }
    }

    if (nNewRow == 0 && !fOldRowFound) {
        nNewRow = ui->tableWidgetMySystemnodes->rowCount();
        ui->tableWidgetMySystemnodes->insertRow(nNewRow);
    }

    QTableWidgetItem* aliasItem = new QTableWidgetItem(strAlias);
    QTableWidgetItem* addrItem = new QTableWidgetItem(pmn ? QString::fromStdString(pmn->addr.ToString()) : strAddr);
    PrivateKeyWidget *privateKeyWidget = new PrivateKeyWidget(privkey);
    QTableWidgetItem* protocolItem = new QTableWidgetItem(QString::number(pmn ? pmn->protocolVersion : -1));
    QTableWidgetItem* statusItem = new QTableWidgetItem(QString::fromStdString(pmn ? pmn->Status() : "MISSING"));
    QTableWidgetItem* activeSecondsItem = new QTableWidgetItem(QString::fromStdString(DurationToDHMS(pmn ? (int64_t)(pmn->lastPing.sigTime - pmn->sigTime) : 0)));
    QTableWidgetItem* lastSeenItem = new QTableWidgetItem(QString::fromStdString(DateTimeStrFormat("%Y-%m-%d %H:%M", pmn ? (int64_t)pmn->lastPing.sigTime : 0)));
    QTableWidgetItem* pubkeyItem = new QTableWidgetItem(QString::fromStdString(pmn ? EncodeDestination(PKHash(pmn->pubkey)) : ""));

    ui->tableWidgetMySystemnodes->setItem(nNewRow, 0, aliasItem);
    ui->tableWidgetMySystemnodes->setItem(nNewRow, 1, addrItem);
    ui->tableWidgetMySystemnodes->setCellWidget(nNewRow, 2, privateKeyWidget);
    ui->tableWidgetMySystemnodes->setColumnWidth(2, 150);
    ui->tableWidgetMySystemnodes->setItem(nNewRow, 3, protocolItem);
    ui->tableWidgetMySystemnodes->setItem(nNewRow, 4, statusItem);
    ui->tableWidgetMySystemnodes->setItem(nNewRow, 5, activeSecondsItem);
    ui->tableWidgetMySystemnodes->setItem(nNewRow, 6, lastSeenItem);
    ui->tableWidgetMySystemnodes->setItem(nNewRow, 7, pubkeyItem);
}

void SystemnodeList::updateMyNodeList(bool fForce)
{
    static int64_t nTimeMyListUpdated = 0;

    // automatically update my systemnode list only once in MY_SYSTEMNODELIST_UPDATE_SECONDS seconds,
    // this update still can be triggered manually at any time via button click
    int64_t nSecondsTillUpdate = nTimeMyListUpdated + MY_SYSTEMNODELIST_UPDATE_SECONDS - GetTime();
    ui->secondsLabel->setText(QString::number(nSecondsTillUpdate));

    if (nSecondsTillUpdate > 0 && !fForce) return;
    nTimeMyListUpdated = GetTime();

    ui->tableWidgetMySystemnodes->setSortingEnabled(false);
    for (CNodeEntry sne : systemnodeConfig.getEntries()) {
        CTxIn txin = CTxIn(uint256S(sne.getTxHash()), uint32_t(atoi(sne.getOutputIndex().c_str())));
        CSystemnode* pmn = snodeman.Find(txin);
        updateMySystemnodeInfo(QString::fromStdString(sne.getAlias()), QString::fromStdString(sne.getIp()), QString::fromStdString(sne.getPrivKey()), QString::fromStdString(sne.getTxHash()),
            QString::fromStdString(sne.getOutputIndex()), pmn);
    }
    ui->tableWidgetMySystemnodes->setSortingEnabled(true);

    // reset "timer"
    ui->secondsLabel->setText("0");
}

void SystemnodeList::on_reloadButton_clicked()
{
    TRY_LOCK(cs_snlistupdate, fLockAcquired);
    if(!fLockAcquired) {
        return;
    }

    ui->tableWidgetMySystemnodes->setSortingEnabled(false);
    ui->tableWidgetMySystemnodes->clearContents();
    ui->tableWidgetMySystemnodes->setRowCount(0);

    loadNodeConfiguration();
    updateMyNodeList(true);
}

void SystemnodeList::updateNodeList()
{
    TRY_LOCK(cs_snlistupdate, fLockAcquired);
    if(!fLockAcquired) {
        return;
    }

    static int64_t nTimeListUpdated = GetTime();

    // to prevent high cpu usage update only once in SYSTEMNODELIST_UPDATE_SECONDS seconds
    // or SYSTEMNODELIST_FILTER_COOLDOWN_SECONDS seconds after filter was last changed
    int64_t nSecondsToWait = fFilterUpdated
                             ? nTimeFilterUpdated - GetTime() + SYSTEMNODELIST_FILTER_COOLDOWN_SECONDS
                             : nTimeListUpdated - GetTime() + SYSTEMNODELIST_UPDATE_SECONDS;

    if(fFilterUpdated) ui->countLabel->setText(QString::fromStdString(strprintf("Please wait... %d", nSecondsToWait)));
    if(nSecondsToWait > 0) return;

    nTimeListUpdated = GetTime();
    fFilterUpdated = false;

    QString strToFilter;
    ui->countLabel->setText("Updating...");
    ui->tableWidgetSystemnodes->setSortingEnabled(false);
    ui->tableWidgetSystemnodes->clearContents();
    ui->tableWidgetSystemnodes->setRowCount(0);
    std::vector<CSystemnode> vSystemnodes = snodeman.GetFullSystemnodeVector();

    for (CSystemnode& sn : vSystemnodes)
    {
        // populate list
        // Address, Protocol, Status, Active Seconds, Last Seen, Pub Key
        QTableWidgetItem *addressItem = new QTableWidgetItem(QString::fromStdString(sn.addr.ToString()));
        QTableWidgetItem *protocolItem = new QTableWidgetItem(QString::number(sn.protocolVersion));
        QTableWidgetItem *statusItem = new QTableWidgetItem(QString::fromStdString(sn.Status()));
        QTableWidgetItem *activeSecondsItem = new QTableWidgetItem(QString::fromStdString(DurationToDHMS(sn.lastPing.sigTime - sn.sigTime)));
        QTableWidgetItem *lastSeenItem = new QTableWidgetItem(QString::fromStdString(DateTimeStrFormat("%Y-%m-%d %H:%M", sn.lastPing.sigTime + QDateTime::currentDateTime().offsetFromUtc())));
        QTableWidgetItem *pubkeyItem = new QTableWidgetItem(QString::fromStdString(EncodeDestination(PKHash(sn.pubkey))));

        if (strCurrentFilter != "")
        {
            strToFilter =   addressItem->text() + " " +
                            protocolItem->text() + " " +
                            statusItem->text() + " " +
                            activeSecondsItem->text() + " " +
                            lastSeenItem->text() + " " +
                            pubkeyItem->text();
            if (!strToFilter.contains(strCurrentFilter)) continue;
        }

        ui->tableWidgetSystemnodes->insertRow(0);
        ui->tableWidgetSystemnodes->setItem(0, 0, addressItem);
        ui->tableWidgetSystemnodes->setItem(0, 1, protocolItem);
        ui->tableWidgetSystemnodes->setItem(0, 2, statusItem);
        ui->tableWidgetSystemnodes->setItem(0, 3, activeSecondsItem);
        ui->tableWidgetSystemnodes->setItem(0, 4, lastSeenItem);
        ui->tableWidgetSystemnodes->setItem(0, 5, pubkeyItem);
    }

    ui->countLabel->setText(QString::number(ui->tableWidgetSystemnodes->rowCount()));
    ui->tableWidgetSystemnodes->setSortingEnabled(true);
}

void SystemnodeList::on_filterLineEdit_textChanged(const QString &strFilterIn)
{
    strCurrentFilter = strFilterIn;
    nTimeFilterUpdated = GetTime();
    fFilterUpdated = true;
    ui->countLabel->setText(QString::fromStdString(strprintf("Please wait... %d", SYSTEMNODELIST_FILTER_COOLDOWN_SECONDS)));
}

void SystemnodeList::on_startButton_clicked()
{
    // Find selected node alias
    QItemSelectionModel* selectionModel = ui->tableWidgetMySystemnodes->selectionModel();
    QModelIndexList selected = selectionModel->selectedRows();

    if (selected.count() == 0) return;

    QModelIndex index = selected.at(0);
    int nSelectedRow = index.row();
    std::string strAlias = ui->tableWidgetMySystemnodes->item(nSelectedRow, 0)->text().toStdString();

    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm systemnode start"),
        tr("Are you sure you want to start systemnode %1?").arg(QString::fromStdString(strAlias)),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if (retval != QMessageBox::Yes) return;

    StartAlias(strAlias);
}

void SystemnodeList::on_startAllButton_clicked()
{
    if (!systemnodeSync.IsSynced()) {
        notReady();
        return;
    }

    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm all systemnodes start"),
        tr("Are you sure you want to start ALL systemnodes?"),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if (retval != QMessageBox::Yes) return;

    StartAll();
}

void SystemnodeList::on_startMissingButton_clicked()
{
    if (!systemnodeSync.IsSynced()) {
        notReady();
        return;
    }

    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this,
        tr("Confirm missing systemnodes start"),
        tr("Are you sure you want to start MISSING systemnodes?"),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if (retval != QMessageBox::Yes) return;

    StartAll("start-missing");
}

void SystemnodeList::on_tableWidgetMySystemnodes_itemSelectionChanged()
{
    if (ui->tableWidgetMySystemnodes->selectedItems().count() > 0) {
        ui->startButton->setEnabled(true);
    }
}

void SystemnodeList::on_UpdateButton_clicked()
{
    if (!systemnodeSync.IsSynced()) {
        notReady();
        return;
    }

    updateMyNodeList(true);
}
