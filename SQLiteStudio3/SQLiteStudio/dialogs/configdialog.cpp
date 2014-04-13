#include "configdialog.h"
#include "ui_configdialog.h"
#include "services/config.h"
#include "uiconfig.h"
#include "customconfigwidgetplugin.h"
#include "services/pluginmanager.h"
#include "formmanager.h"
#include "sqlformatter.h"
#include "plugins/sqlformatterplugin.h"
#include "configwidgets/styleconfigwidget.h"
#include "configwidgets/combodatawidget.h"
#include "iconmanager.h"
#include "common/userinputfilter.h"
#include "mainwindow.h"
#include "common/unused.h"
#include "sqlitestudio.h"
#include "configmapper.h"
#include <QSignalMapper>
#include <QLineEdit>
#include <QSpinBox>
#include <QDebug>
#include <QComboBox>
#include <QStyleFactory>
#include <QFile>
#include <QDir>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QListWidget>
#include <QTableWidget>
#include <QtUiTools/QUiLoader>

#define GET_FILTER_STRING(Widget, WidgetType, Method) \
    if (qobject_cast<WidgetType*>(Widget))\
        return qobject_cast<WidgetType*>(Widget)->Method() + " " + Widget->toolTip();\

#define GET_FILTER_STRING2(Widget, WidgetType) \
    WidgetType* w##WidgetType = qobject_cast<WidgetType*>(widget);\
    if (w##WidgetType)\
        return getFilterString(w##WidgetType) + " " + Widget->toolTip();

ConfigDialog::ConfigDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ConfigDialog)
{
    init();
}

ConfigDialog::~ConfigDialog()
{
    delete ui;
    safe_delete(configMapper);
}

QString ConfigDialog::getFilterString(QWidget *widget)
{
    // Common code for widgets with single method call
    GET_FILTER_STRING(widget, QLabel, text);
    GET_FILTER_STRING(widget, QAbstractButton, text);
    GET_FILTER_STRING(widget, QLineEdit, text);
    GET_FILTER_STRING(widget, QTextEdit, toPlainText);
    GET_FILTER_STRING(widget, QPlainTextEdit, toPlainText);
    GET_FILTER_STRING(widget, QGroupBox, title);

    // Widgets needs a little more than single method call
    GET_FILTER_STRING2(widget, QComboBox);
    GET_FILTER_STRING2(widget, QTreeWidget);
    GET_FILTER_STRING2(widget, QListWidget);
    GET_FILTER_STRING2(widget, QTableWidget);

    return QString::null;
}

QString ConfigDialog::getFilterString(QComboBox *widget)
{
    QStringList items;
    for (int i = 0; i < widget->count(); i++)
        items << widget->itemText(i);

    return items.join(" ");
}

QString ConfigDialog::getFilterString(QTreeWidget *widget)
{
    QList<QTreeWidgetItem*> items = widget->findItems("*", Qt::MatchWildcard|Qt::MatchRecursive);
    QStringList strList;
    foreach (QTreeWidgetItem* item, items)
        for (int i = 0; i < widget->columnCount(); i++)
            strList << item->text(i) + " " + item->toolTip(0);

    return strList.join(" ");
}

QString ConfigDialog::getFilterString(QListWidget *widget)
{
    QList<QListWidgetItem*> items = widget->findItems("*", Qt::MatchWildcard|Qt::MatchRecursive);
    QStringList strList;
    foreach (QListWidgetItem* item, items)
        strList << item->text() + " " + item->toolTip();

    return strList.join(" ");
}

QString ConfigDialog::getFilterString(QTableWidget *widget)
{
    QList<QTableWidgetItem*> items = widget->findItems("*", Qt::MatchWildcard|Qt::MatchRecursive);
    QStringList strList;
    foreach (QTableWidgetItem* item, items)
         strList << item->text() + " " + item->toolTip();

    return strList.join(" ");
}

void ConfigDialog::init()
{
    ui->setupUi(this);
    setWindowIcon(ICONS.CONFIGURE);

    configMapper = new ConfigMapper(CfgMain::getPersistableInstances());
    connect(configMapper, SIGNAL(modified()), this, SLOT(modified()));

    ui->categoriesFilter->setClearButtonEnabled(true);
    UserInputFilter* filter = new UserInputFilter(ui->categoriesFilter, this, SLOT(applyFilter(QString)));
    filter->setDelay(500);

    ui->stackedWidget->setCurrentWidget(ui->generalPage);
    initPageMap();
    initInternalCustomConfigWidgets();
    initPlugins();
    initPluginsPage();
    initFormatterPlugins();

    connect(ui->categoriesTree, SIGNAL(currentItemChanged(QTreeWidgetItem*,QTreeWidgetItem*)), this, SLOT(switchPage(QTreeWidgetItem*)));
    connect(ui->previewTabs, SIGNAL(currentChanged(int)), this, SLOT(updateStylePreview()));
    connect(ui->activeStyleCombo, SIGNAL(currentTextChanged(QString)), this, SLOT(updateStylePreview()));
    connect(ui->buttonBox->button(QDialogButtonBox::Apply), SIGNAL(clicked()), this, SLOT(apply()));

    ui->activeStyleCombo->addItems(QStyleFactory::keys());

    load();
    updateStylePreview();
}

void ConfigDialog::load()
{
    configMapper->loadToWidget(ui->stackedWidget);
    setModified(false);
}

void ConfigDialog::save()
{
    MainWindow::getInstance()->setStyle(ui->activeStyleCombo->currentText());

    QString loadedPlugins = collectLoadedPlugins();
    CFG_CORE.General.LoadedPlugins.set(loadedPlugins);

    configMapper->saveFromWidget(ui->stackedWidget);
}

void ConfigDialog::modified()
{
    setModified(true);
}

void ConfigDialog::setModified(bool modified)
{
    ui->buttonBox->button(QDialogButtonBox::Apply)->setEnabled(modified);
}

void ConfigDialog::applyFilter(const QString &filter)
{
    QColor normalColor = ui->categoriesTree->palette().color(QPalette::Active, QPalette::WindowText);
    QColor disabledColor = ui->categoriesTree->palette().color(QPalette::Disabled, QPalette::WindowText);
    if (filter.isEmpty())
    {
        foreach (QTreeWidgetItem* item, getAllCategoryItems())
            item->setForeground(0, normalColor);

        return;
    }

    QList<QWidget*> widgets = ui->stackedWidget->findChildren<QWidget*>();
    QList<QWidget*> matchedWidgets;
    foreach (QWidget* widget, widgets)
    {
        if (getFilterString(widget).contains(filter, Qt::CaseInsensitive))
            matchedWidgets << widget;
    }

    QHash<QWidget*, QTreeWidgetItem*> pageToCategoryItem = buildPageToCategoryItemMap();
    QSet<QTreeWidgetItem*> matchedCategories;
    foreach (QWidget* page, pageToCategoryItem.keys())
    {
        foreach (QWidget* matched, matchedWidgets)
        {
            if (page->isAncestorOf(matched))
            {
                if (!pageToCategoryItem.contains(page))
                {
                    qCritical() << "Page" << page << "not on page-to-category-item mapping.";
                    continue;
                }

                matchedCategories << pageToCategoryItem[page];
                break;
            }
        }
    }

    foreach (QTreeWidgetItem* item, getAllCategoryItems())
        item->setForeground(0, disabledColor);

    foreach (QTreeWidgetItem* item, matchedCategories)
    {
        item->setForeground(0, normalColor);
        while ((item = item->parent()) != nullptr)
            item->setForeground(0, normalColor);
    }
}

QHash<QWidget*, QTreeWidgetItem*> ConfigDialog::buildPageToCategoryItemMap() const
{
    QHash<QString,QTreeWidgetItem*> pageNameToCategoryItem;
    foreach (QTreeWidgetItem* item, getAllCategoryItems())
        pageNameToCategoryItem[item->statusTip(0)] = item;

    QWidget* page;
    QHash<QWidget*,QTreeWidgetItem*> pageToCategoryItem;
    for (int i = 0; i < ui->stackedWidget->count(); i++)
    {
        page = ui->stackedWidget->widget(i);
        pageToCategoryItem[page] = pageNameToCategoryItem[page->objectName()];
    }
    return pageToCategoryItem;
}

QList<QTreeWidgetItem *> ConfigDialog::getAllCategoryItems() const
{
    return ui->categoriesTree->findItems("*", Qt::MatchWildcard|Qt::MatchRecursive);
}

void ConfigDialog::updateActiveFormatterState()
{
    ui->activeFormatterConfigButton->setEnabled(ui->activeFormatterCombo->currentIndex() > -1);
}

void ConfigDialog::activeFormatterChanged(const QString& title)
{
    UNUSED(title);
    updateActiveFormatterState();
}

void ConfigDialog::activeFormatterConfigurePressed()
{
    QString title = ui->activeFormatterCombo->currentText();
    QTreeWidgetItem* item = getItemByTitle(title);
    if (!item)
        return;

    ui->categoriesTree->setCurrentItem(item);
}

void ConfigDialog::detailsClicked(QString pluginName)
{
    static const QString details = QStringLiteral(
            "<table>"
                "<thead>"
                    "<tr><td colspan=2 align=\"center\"><b>%1</b></td></tr>"
                "</thead>"
                "<tbody>%2</tbody>"
            "</table>");
    static const QString row = QStringLiteral("<tr><td>%1</td><td align=\"right\">%2</td></tr>");

    PluginType* type = PLUGINS->getPluginType(pluginName);
    Q_ASSERT(type != nullptr);

    // Rows
    QStringList rows;
    rows << row.arg(tr("Description:", "plugin details")).arg(PLUGINS->getDescription(pluginName));
    rows << row.arg(tr("Category:", "plugin details")).arg(type->getTitle());
    rows << row.arg(tr("Version:", "plugin details")).arg(PLUGINS->getPrintableVersion(pluginName));
    rows << row.arg(tr("Author:", "plugin details")).arg(PLUGINS->getAuthor(pluginName));
    rows << row.arg(tr("Technical name:", "plugin details")).arg(pluginName);

    // Message
    QString pluginDetails = details.arg(PLUGINS->getTitle(pluginName)).arg(rows.join(""));
    QMessageBox::information(this, tr("Plugin details"), pluginDetails);
}

void ConfigDialog::sqlFormatterAboutToUnload(Plugin* plugin)
{
    QString title = plugin->getTitle();
    if (ui->activeFormatterCombo->currentText() != title)
        return;

    if (ui->activeFormatterCombo->count() > 0)
        ui->activeFormatterCombo->setCurrentIndex(0);

    ui->activeFormatterCombo->setCurrentIndex(-1);
}

void ConfigDialog::sqlFormatterLoaded(Plugin* plugin)
{
    ui->activeFormatterCombo->addItem(plugin->getTitle(), plugin->getName());
    configMapper->loadToWidget(CFG_CORE.General.ActiveSqlFormatter, ui->activeFormatterCombo);
}

void ConfigDialog::loadUnloadPlugin(QTreeWidgetItem* item, int column)
{
    if (column != 0)
        return;

    QString pluginName = itemToPluginNameMap[item];
    if (PLUGINS->isBuiltIn(pluginName))
        return;

    bool wasLoaded = PLUGINS->isLoaded(pluginName);

    if (wasLoaded == (item->checkState(0) == Qt::Checked))
        return;

    if (wasLoaded)
        PLUGINS->unload(pluginName);
    else
        PLUGINS->load(pluginName);

    modified();
}

void ConfigDialog::pluginAboutToUnload(Plugin* plugin, PluginType* type)
{
    // Update formatters page
    if (type->isForPluginType<SqlFormatterPlugin>())
        sqlFormatterAboutToUnload(plugin);

    // Deinit tree item
    QTreeWidgetItem* typeItem = getPluginsCategoryItem(type);
    QTreeWidgetItem* pluginItem = getPluginItem(plugin);
    if (pluginItem)
    {
        typeItem->removeChild(pluginItem);
        pluginToItemMap.remove(plugin);
    }

    // Deinit page
    deinitPluginPage(plugin->getName());

    // Update tree categories
    updatePluginCategoriesVisibility();
}

void ConfigDialog::pluginLoaded(Plugin* plugin, PluginType* type)
{
    if (plugin->getConfigUiForm().isNull())
        return;

    // Update formatters page
    if (type->isForPluginType<SqlFormatterPlugin>())
        sqlFormatterLoaded(plugin);

    // Init tree item
    QTreeWidgetItem* typeItem = getPluginsCategoryItem(type);
    QTreeWidgetItem* pluginItem = new QTreeWidgetItem({plugin->getTitle()});
    pluginItem->setStatusTip(0, plugin->getName());
    typeItem->addChild(pluginItem);
    pluginToItemMap[plugin] = pluginItem;

    // Init page
    initPluginPage(plugin->getName());

    // Update tree categories
    updatePluginCategoriesVisibility();
}

void ConfigDialog::updatePluginCategoriesVisibility()
{
    QTreeWidgetItem* categories = getPluginsCategoryItem();
    for (int i = 0; i < categories->childCount(); i++)
        updatePluginCategoriesVisibility(categories->child(i));
}

void ConfigDialog::updatePluginCategoriesVisibility(QTreeWidgetItem* categoryItem)
{
    categoryItem->setHidden(categoryItem->childCount() == 0);
}

QString ConfigDialog::collectLoadedPlugins() const
{
    QStringList loaded;
    QHashIterator<QTreeWidgetItem*,QString> it(itemToPluginNameMap);
    while (it.hasNext())
    {
        it.next();
        loaded << (it.value() + "=" + ((it.key()->checkState(0) == Qt::Checked) ? "1" : "0"));
    }

    return loaded.join(",");
}

void ConfigDialog::initPageMap()
{
    int pages = ui->stackedWidget->count();
    QWidget* widget;
    for (int i = 0; i < pages; i++)
    {
        widget = ui->stackedWidget->widget(i);
        nameToPage[widget->objectName()] = widget;
    }
}

void ConfigDialog::initInternalCustomConfigWidgets()
{
    internalCustomConfigWidgets << new StyleConfigWidget();
    internalCustomConfigWidgets << new ComboDataWidget(&CFG_CORE.General.ActiveSqlFormatter);
}

void ConfigDialog::initFormatterPlugins()
{
    SqlFormatterPlugin* formatter = SQLITESTUDIO->getSqlFormatter()->getFormatter();
    if (!formatter)
        ui->activeFormatterCombo->setCurrentIndex(-1);
    else
        ui->activeFormatterCombo->setCurrentText(formatter->getTitle());

    connect(ui->activeFormatterCombo, SIGNAL(currentTextChanged(QString)), this, SLOT(activeFormatterChanged(QString)));
    connect(ui->activeFormatterConfigButton, SIGNAL(clicked()), this, SLOT(activeFormatterConfigurePressed()));

    updateActiveFormatterState();
}

void ConfigDialog::applyStyle(QWidget *widget, QStyle *style)
{
    widget->setStyle(style);
    foreach (QObject* child, widget->children())
    {
        if (!qobject_cast<QWidget*>(child))
            continue;

        applyStyle(qobject_cast<QWidget*>(child), style);
    }
}

QTreeWidgetItem* ConfigDialog::getPluginsCategoryItem() const
{
    QTreeWidgetItem* item = nullptr;
    for (int i = 0; i < ui->categoriesTree->topLevelItemCount(); i++)
    {
         item = ui->categoriesTree->topLevelItem(i);
         if (item->statusTip(0) == ui->pluginsPage->objectName())
             return item;
    }
    Q_ASSERT_X(true, "ConfigDialog", "No Plugins toplevel item in config categories tree!");
    return nullptr;
}

QTreeWidgetItem* ConfigDialog::getPluginsCategoryItem(PluginType* type) const
{
    if (!pluginTypeToItemMap.contains(type))
        return nullptr;

    return pluginTypeToItemMap[type];
}

QTreeWidgetItem* ConfigDialog::getPluginItem(Plugin* plugin) const
{
    if (!pluginToItemMap.contains(plugin))
        return nullptr;

    return pluginToItemMap[plugin];
}

QTreeWidgetItem* ConfigDialog::createPluginsTypeItem(const QString& widgetName, const QString& title) const
{
    if (FORMS->hasWidget(widgetName))
        return new QTreeWidgetItem({title});

    QTreeWidgetItem* pluginsCategoryItem = getPluginsCategoryItem();
    QTreeWidgetItem* item = nullptr;
    for (int i = 0; i < pluginsCategoryItem->childCount(); i++)
    {
        item = pluginsCategoryItem->child(i);
        if (item->statusTip(0) == widgetName)
            return item;
    }
    return nullptr;

}

QTreeWidgetItem* ConfigDialog::getItemByTitle(const QString& title) const
{
    QList<QTreeWidgetItem*> items = ui->categoriesTree->findItems(title, Qt::MatchExactly|Qt::MatchRecursive);
    if (items.size() == 0)
        return nullptr;

    return items.first();
}

void ConfigDialog::switchPage(QTreeWidgetItem *item)
{
    if (isPluginCategoryItem((item)))
    {
        switchPageToPlugin(item);
        return;
    }

    QString name = item->statusTip(0);
    if (!nameToPage.contains(name))
    {
        qWarning() << "Switched page to item" << name << "but there's no such named page defined in ConfigDialog.";
        return;
    }

    ui->stackedWidget->setCurrentWidget(nameToPage[name]);
}

void ConfigDialog::switchPageToPlugin(QTreeWidgetItem *item)
{
    QString pluginName = item->statusTip(0);
    if (!nameToPage.contains(pluginName))
    {
        qCritical() << "No plugin page available for plugin:" << pluginName;
        return;
    }
    ui->stackedWidget->setCurrentWidget(nameToPage[pluginName]);
}

void ConfigDialog::initPlugins()
{
    QTreeWidgetItem *item = getPluginsCategoryItem();

    // Recreate
    QTreeWidgetItem *typeItem;
    foreach (PluginType* pluginType, PLUGINS->getPluginTypes())
    {
        typeItem = createPluginsTypeItem(pluginType->getConfigUiForm(), pluginType->getTitle());
        if (!typeItem)
            continue;

        item->addChild(typeItem);
        pluginTypeToItemMap[pluginType] = typeItem;

        foreach (Plugin* plugin, pluginType->getLoadedPlugins())
            pluginLoaded(plugin, pluginType);
    }

    updatePluginCategoriesVisibility();

    connect(PLUGINS, SIGNAL(loaded(Plugin*,PluginType*)), this, SLOT(pluginLoaded(Plugin*,PluginType*)));
    connect(PLUGINS, SIGNAL(aboutToUnload(Plugin*,PluginType*)), this, SLOT(pluginAboutToUnload(Plugin*,PluginType*)));
}

void ConfigDialog::initPluginsPage()
{
    QTreeWidgetItem* category;
    QTreeWidgetItem* item;
    QFont font;
    QModelIndex categoryIndex;
    QModelIndex itemIndex;
    int itemRow;
    int categoryRow;
    bool builtIn;
    QLabel* detailsLabel;
    QString title;
    QSize itemSize;
    QStringList pluginNames;

    // Font and metrics
    item = new QTreeWidgetItem({""});
    font = item->font(0);

    QFontMetrics fm(font);
    itemSize = QSize(-1, (fm.ascent() + fm.descent() + 4));

    delete item;

    // Creating...
    ui->pluginsPageInfoIcon->setPixmap(ICONS.INFO_BALLOON);

    ui->pluginsList->header()->setSectionsMovable(false);
    ui->pluginsList->header()->setSectionResizeMode(0, QHeaderView::Stretch);

    QBrush categoryBg = ui->pluginsList->palette().button();
    QBrush categoryFg = ui->pluginsList->palette().buttonText();

    categoryRow = 0;
    QList<PluginType*> pluginTypes = PLUGINS->getPluginTypes();
    qSort(pluginTypes.begin(), pluginTypes.end(), PluginType::nameLessThan);
    foreach (PluginType* pluginType, pluginTypes)
    {
        category = new QTreeWidgetItem({pluginType->getTitle()});
        font.setItalic(false);
        font.setBold(true);
        category->setFont(0, font);
        for (int i = 0; i < 2; i++)
        {
            category->setBackground(i, categoryBg);
            category->setForeground(i, categoryFg);
        }
        category->setSizeHint(0, itemSize);
        ui->pluginsList->addTopLevelItem(category);

        categoryIndex = ui->pluginsList->model()->index(categoryRow, 0);
        categoryRow++;

        itemRow = 0;
        pluginNames = pluginType->getAllPluginNames();
        qSort(pluginNames);
        foreach (const QString& pluginName, pluginNames)
        {
            builtIn = PLUGINS->isBuiltIn(pluginName);
            title = PLUGINS->getTitle(pluginName);
            if (builtIn)
                title += tr(" (built-in)", "plugins manager in configuration dialog");

            item = new QTreeWidgetItem({title});
            item->setSizeHint(0, itemSize);
            item->setCheckState(0, PLUGINS->isLoaded(pluginName) ? Qt::Checked : Qt::Unchecked);
            item->setSizeHint(0, itemSize);
            if (builtIn)
                item->setDisabled(true);

            category->addChild(item);

            connect(ui->pluginsList, SIGNAL(itemChanged(QTreeWidgetItem*,int)), this, SLOT(loadUnloadPlugin(QTreeWidgetItem*,int)));
            itemToPluginNameMap[item] = pluginName;

            // Details button
            detailsLabel = new QLabel(QString("<a href='%1'>%2</a>").arg(pluginName).arg(tr("Details")), ui->pluginsList);
            detailsLabel->setAlignment(Qt::AlignRight);
            itemIndex = ui->pluginsList->model()->index(itemRow, 1, categoryIndex);
            ui->pluginsList->setIndexWidget(itemIndex, detailsLabel);

            connect(detailsLabel, SIGNAL(linkActivated(QString)), this, SLOT(detailsClicked(QString)));

            itemRow++;
        }

        if (itemRow == 0)
        {
            item = new QTreeWidgetItem({tr("No plugins in this category.")});
            item->setDisabled(true);
            item->setSizeHint(0, itemSize);

            font.setItalic(true);
            font.setBold(false);
            item->setFont(0, font);

            category->addChild(item);
        }

        category->setExpanded(true);
    }
}

void ConfigDialog::initPluginPage(const QString& pluginName)
{
    QWidget* widget = FORMS->createWidget(pluginName);
    if (!widget)
    {
        qWarning() << "Could not load plugin UI file for plugin:" << pluginName;
        return;
    }

    nameToPage[pluginName] = widget;
    ui->stackedWidget->addWidget(widget);
    configMapper->loadToWidget(widget);
}

void ConfigDialog::deinitPluginPage(const QString& pluginName)
{
    if (!nameToPage.contains(pluginName))
        return;

    QWidget* widget = nameToPage[pluginName];
    nameToPage.remove(pluginName);
    ui->stackedWidget->removeWidget(widget);
    delete widget;
}

bool ConfigDialog::isPluginCategoryItem(QTreeWidgetItem *item) const
{
    return item->parent() && item->parent()->parent() && item->parent()->parent() == getPluginsCategoryItem();
}

void ConfigDialog::updateStylePreview()
{
    ui->previewWidget->parentWidget()->layout()->removeWidget(ui->previewWidget);
    ui->previewTabs->currentWidget()->layout()->addWidget(ui->previewWidget);
    ui->previewWidget->setEnabled(ui->previewTabs->currentIndex() == 0);

    QStyle* previousStyle = previewStyle;
    previewStyle = QStyleFactory::create(ui->activeStyleCombo->currentText());
    if (!previewStyle)
    {
        qWarning() << "Could not create style:" << ui->activeStyleCombo->currentText();
        return;
    }

    applyStyle(ui->activeStylePreviewGroup, previewStyle);

    if (previousStyle)
        delete previousStyle;
}

void ConfigDialog::apply()
{
    setModified(false);
    save();
}

void ConfigDialog::accept()
{
    apply();
    QDialog::accept();
}
