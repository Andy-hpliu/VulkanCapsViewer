/*
*
* Vulkan hardware capability viewer
*
* Copyright (C) 2016 by Sascha Willems (www.saschawillems.de)
*
* This code is free software, you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public
* License version 3 as published by the Free Software Foundation.
*
* Please review the following information to ensure the GNU Lesser
* General Public License version 3 requirements will be met:
* http://opensource.org/licenses/lgpl-3.0.html
*
* The code is distributed WITHOUT ANY WARRANTY; without even the
* implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
* PURPOSE.  See the GNU LGPL 3.0 for more details.
*
*/

#include "vulkancapsviewer.h"
#include "vulkanresources.h"
#include <typeinfo>
#include <QMessageBox>
#include <QFileDialog>
#include <QStyleFactory>
#include <QDesktopServices>
#include <QUrl>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QHttpMultiPart>
#include <QVariant>
#include <QInputDialog>
#include <QSysInfo>
#include <QDebug>
#include <qnamespace.h>
#include <assert.h>
#include <settingsDialog.h>
#include "submitDialog.h"
#ifdef _WIN32
#include <Windows.h>
#endif 
#ifdef __linux__
#include <sys/utsname.h>
#endif

#define VK_API_VERSION VK_MAKE_VERSION(1, 0, 3)

QTreeWidgetItem *addTreeItem(QTreeWidgetItem *parent, std::string key, std::string value)
{
    QTreeWidgetItem *newItem = new QTreeWidgetItem(parent);
    newItem->setText(0, QString::fromStdString(key));
    newItem->setText(1, QString::fromStdString(value));
    parent->addChild(newItem);
    return newItem;
}

QTreeWidgetItem *addTreeItemVkBool32(QTreeWidgetItem *parent, std::string key, VkBool32 value)
{
    QTreeWidgetItem *newItem = new QTreeWidgetItem(parent);
    newItem->setText(0, QString::fromStdString(key));
    newItem->setText(1, (value) ? "true" : "false");
    newItem->setTextColor(1, (value) ? QColor::fromRgb(0, 128, 0) : QColor::fromRgb(255, 0, 0));
    parent->addChild(newItem);
    return newItem;
}

void addFlagTreeItem(QTreeWidgetItem *parent, QString flagName, bool flag)
{
    if (flag)
    {
        QTreeWidgetItem *newItem = new QTreeWidgetItem(parent);
        newItem->setText(0, flagName);
        parent->addChild(newItem);
    }
}

void addFlagModelItem(QStandardItem *parent, QString flagName, bool flag)
{
    if (flag)
    {
        QList<QStandardItem *> flagItems;
        flagItems << new QStandardItem(flagName);
        parent->appendRow(flagItems);
    }
}

/// <summary>
///	Returns operating system name
/// </summary>
OSInfo getOperatingSystem()
{
    // QSysInfo works for all supported operating systems
	OSInfo osInfo = {};
    osInfo.name = QSysInfo::productType().toStdString();
    osInfo.architecture = QSysInfo::currentCpuArchitecture().toStdString();
    osInfo.version = QSysInfo::productVersion().toStdString();
	return osInfo;
}

vulkanCapsViewer::vulkanCapsViewer(QWidget *parent)
	: QMainWindow(parent)
{
	ui.setupUi(this);

    setWindowTitle("Vulkan Hardware Capability Viewer " + QString::fromStdString(version) + " (beta)");

	// Connect slots
    connect(ui.comboBoxGPU, SIGNAL(currentIndexChanged(int)), this, SLOT(slotComboBoxGPUIndexChanged(int)));
    connect(ui.toolButtonUpload, SIGNAL(pressed()), this, SLOT(slotUploadReport()));
    connect(ui.toolButtonSave, SIGNAL(pressed()), this, SLOT(slotSaveReport()));
    connect(ui.toolButtonOnlineDevice, SIGNAL(pressed()), this, SLOT(slotDisplayOnlineReport()));
    connect(ui.toolButtonOnlineDataBase, SIGNAL(pressed()), this, SLOT(slotBrowseDatabase()));
    connect(ui.toolButtonAbout, SIGNAL(pressed()), this, SLOT(slotAbout()));
    connect(ui.toolButtonExit, SIGNAL(pressed()), this, SLOT(slotClose()));
    connect(ui.toolButtonSettings, SIGNAL(pressed()), this, SLOT(slotSettings()));

    qApp->setStyle(QStyleFactory::create("Fusion"));

    vulkanApiVersion = QString::fromStdString(vulkanResources::versionToString(VK_API_VERSION));

#ifdef ANDROID
    // Vulkan is not part of android (yet), so we need to load the library manually
    if (!loadVulkanLibrary())
    {
        QMessageBox::warning(this, "Error", "Could not load Vulkan library!\nDevice must support Vulkan API version " + vulkanApiVersion + "!");
        exit(EXIT_FAILURE);
    }
#endif

	if (!initVulkan())
	{
        QMessageBox::warning(this, "Error", "Could not init Vulkan!\nDevice must support Vulkan API version " + vulkanApiVersion + "!");
        exit(EXIT_FAILURE);
	}

	appSettings.restore();

	// Models and filters
	// Limits
	ui.treeViewDeviceLimits->setModel(&filterProxies.limits);
	filterProxies.limits.setSourceModel(&models.limits);
	connect(ui.filterLineEditLimits, SIGNAL(textChanged(QString)), this, SLOT(slotFilterLimits(QString)));
	// Features
	ui.treeViewDeviceFeatures->setModel(&filterProxies.features);
	filterProxies.features.setSourceModel(&models.features);
	connect(ui.filterLineEditFeatures, SIGNAL(textChanged(QString)), this, SLOT(slotFilterFeatures(QString)));
	// Formats
	ui.treeViewFormats->setModel(&filterProxies.formats);
	filterProxies.formats.setSourceModel(&models.formats);
	connect(ui.filterLineEditFormats, SIGNAL(textChanged(QString)), this, SLOT(slotFilterFormats(QString)));

	getGPUs();
}

vulkanCapsViewer::~vulkanCapsViewer()
{
	//
}

/// <summary>
///	Close the application
/// </summary>
void vulkanCapsViewer::slotClose()
{
	close();
}

/// <summary>
///	Display database in default browser
/// </summary>
void vulkanCapsViewer::slotBrowseDatabase() 
{
	QString link = "http://vulkan.gpuinfo.org/";
	QDesktopServices::openUrl(QUrl(link));
}

/// <summary>
///	Display device report in default browser
/// </summary>
void vulkanCapsViewer::slotDisplayOnlineReport()
{
	int reportId = databaseConnection.getReportId(vulkanGPUs[selectedDeviceIndex]);
	stringstream ss;
	ss << databaseConnection.getBaseUrl() << "displayreport.php?id=" << reportId;
	QDesktopServices::openUrl(QUrl(QString::fromStdString(ss.str())));
}

/// <summary>
///	Refresh GPU list
/// </summary>
void vulkanCapsViewer::slotRefresh()
{
	// getGPUs(); TODO : Clean up before refresh
}

/// <summary>
///	Display an about box
/// </summary>
void vulkanCapsViewer::slotAbout()
{
	std::stringstream aboutText;
    aboutText << "<p>Vulkan Hardware Capability Viewer " << version << "<br/><br/>"
        "Copyright (c) 2016 by <a href='http://www.saschawillems.de'>Sascha Willems</a><br/><br/>"
        "Build against Vulkan API " + vulkanApiVersion.toStdString() +
        " header version " + std::to_string(VK_HEADER_VERSION) + "<br/><br/>"
		"This tool is <b>FREEWARE</b><br/><br/>"
		"For usage and distribution details refer to the readme<br/><br/><br/>"
		"<a href='http://www.gpuinfo.org'>http://www.gpuinfo.org</a><br><br>";
	aboutText << "</p>";
	QMessageBox::about(this, tr("About the Vulkan hardware capability viewer"), QString::fromStdString(aboutText.str()));
}

/// <summary>
///	GPU selection changed
/// </summary>
void vulkanCapsViewer::slotComboBoxGPUIndexChanged(int index)
{
	if (index != selectedDeviceIndex)
	{
		displayDevice(index);
	}
}

/// <summary>
///	Save report to disk (JSON)
/// </summary>
void vulkanCapsViewer::slotSaveReport()
{
	QString fileName = QFileDialog::getSaveFileName(this, tr("Save Report to disk"), "vulkanreport.json", tr("json (*.json)"));
	if (!fileName.isEmpty())
	{
		exportReportAsJSON(fileName.toStdString(), "", "");
	}
}

/// <summary>
///	Upload report to online database
/// </summary>
void vulkanCapsViewer::slotUploadReport()
{
	VulkanDeviceInfo device = vulkanGPUs[selectedDeviceIndex];

	bool dbstatus = databaseConnection.checkServerConnection();
	if (!dbstatus)
	{
		QMessageBox::warning(this, "Error", "Database unreachable!");
		return;
	}

	int reportId = databaseConnection.getReportId(device);
	if (reportId > -1)
	{ 
		QMessageBox::StandardButton reply;
		reply = QMessageBox::question(this, "Device already present", "A report for the selected device is aleady present in the database.\n\nDo you want to open the report in your browser?", QMessageBox::Yes | QMessageBox::No);
		if (reply == QMessageBox::Yes) 
		{
            QString url = QString::fromStdString(databaseConnection.getBaseUrl() + "displayreport.php?id=" + to_string(reportId));
			QDesktopServices::openUrl(QUrl(url));
		}
		return;
	}

	submitDialog dialog(appSettings.submitterName);
	bool ok = (dialog.exec() == QDialog::Accepted);

	if (ok)
	{
		exportReportAsJSON("vulkanreport.json", dialog.getSubmitter(), dialog.getComment());
		std::ostringstream sstream(std::ios::out | std::ios::binary);
		std::ifstream inFile("vulkanreport.json");
		std::string line;
		while (std::getline(inFile, line)) sstream << line << "\r\n";

		string reply = databaseConnection.postReport(sstream.str());
		if (reply == "res_uploaded")
		{
			QMessageBox::information(this, "Report submitted", "Your report has been uploaded to the database!\n\nThanks for your contribution!");
			checkReportDatabaseState();
		}
		else
		{
			QMessageBox::warning(this, "Error", "The report could not be uploaded : \n" + QString::fromStdString(reply));
		}
	}
}

void vulkanCapsViewer::slotSettings()
{
	settingsDialog dialog(appSettings);
	dialog.setModal(true);
	dialog.exec();
	appSettings.restore();
}

void vulkanCapsViewer::slotFilterLimits(QString text)
{
	QRegExp regExp(text, Qt::CaseInsensitive, QRegExp::RegExp);
	filterProxies.limits.setFilterRegExp(regExp);
}

void vulkanCapsViewer::slotFilterFeatures(QString text)
{
	QRegExp regExp(text, Qt::CaseInsensitive, QRegExp::RegExp);
	filterProxies.features.setFilterRegExp(regExp);
}

void vulkanCapsViewer::slotFilterFormats(QString text)
{
	QRegExp regExp(text, Qt::CaseInsensitive, QRegExp::RegExp);
	filterProxies.formats.setFilterRegExp(regExp);
}

void vulkanCapsViewer::getGlobalExtensions()
{
	VkResult vkRes;
	do 
	{
		uint32_t extCount;
		vkRes = vkEnumerateInstanceExtensionProperties(NULL, &extCount, NULL);
		assert(!vkRes);
		std::vector<VkExtensionProperties> extensions(extCount);
		vkRes = vkEnumerateInstanceExtensionProperties(NULL, &extCount, &extensions.front());
		globalInfo.extensions.insert(globalInfo.extensions.end(), extensions.begin(), extensions.end());
	} while (vkRes == VK_INCOMPLETE);
	assert(!vkRes);
}

void vulkanCapsViewer::displayGlobalExtensions()
{
	QTreeWidget *tree = ui.treeWidgetGlobalExtenssions;

	for (auto& globalExt : globalInfo.extensions)
	{
		QTreeWidgetItem *treeItem = new QTreeWidgetItem(tree);
		treeItem->setText(0, QString::fromLatin1(globalExt.extensionName));
		treeItem->setText(1, QString::fromStdString(vulkanResources::versionToString(globalExt.specVersion)));
	}
	for (int i = 0; i < tree->columnCount(); i++)
	{
		tree->header()->setSectionResizeMode(i, QHeaderView::ResizeToContents);
	}
}

void vulkanCapsViewer::displaySurfaceProperties(VulkanDeviceInfo *device)
{    
    if (vkSurface == VK_NULL_HANDLE)
    {
        return;
    }

    QTreeWidget *treeWidget = ui.treeWidgetSurface;
    treeWidget->clear();

    // Surface capabilities
    VkSurfaceCapabilitiesKHR surfaceCaps;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device->device, vkSurface, &surfaceCaps) == VK_SUCCESS)
    {
        QTreeWidgetItem *surfaceCapsItem = addTreeItem(treeWidget->invisibleRootItem(), "Surface Capabilities", "");
        QTreeWidgetItem *flagsItem;

        // Usage flags
        flagsItem = addTreeItem(surfaceCapsItem, "Supported usage flags", "");
        if (surfaceCaps.supportedUsageFlags == 0)
        {
            addTreeItem(flagsItem, "none", "");
        }
        else
        {
            addFlagTreeItem(flagsItem, "TRANSFER_SRC_BIT", (VK_IMAGE_USAGE_TRANSFER_SRC_BIT & surfaceCaps.supportedUsageFlags));
            addFlagTreeItem(flagsItem, "TRANSFER_DST_BIT", (VK_IMAGE_USAGE_TRANSFER_DST_BIT & surfaceCaps.supportedUsageFlags));
            addFlagTreeItem(flagsItem, "SAMPLED_BIT", (VK_IMAGE_USAGE_SAMPLED_BIT & surfaceCaps.supportedUsageFlags));
            addFlagTreeItem(flagsItem, "STORAGE_BIT", (VK_IMAGE_USAGE_STORAGE_BIT & surfaceCaps.supportedUsageFlags));
            addFlagTreeItem(flagsItem, "COLOR_ATTACHMENT_BIT", (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT & surfaceCaps.supportedUsageFlags));
            addFlagTreeItem(flagsItem, "DEPTH_STENCIL_ATTACHMENT_BIT", (VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT & surfaceCaps.supportedUsageFlags));
            addFlagTreeItem(flagsItem, "TRANSIENT_ATTACHMENT_BIT", (VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT & surfaceCaps.supportedUsageFlags));
            addFlagTreeItem(flagsItem, "USAGE_INPUT_ATTACHMENT_BIT", (VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT & surfaceCaps.supportedUsageFlags));
        }

        // Transform flags
        flagsItem = addTreeItem(surfaceCapsItem, "Supported transforms", "");
        if (surfaceCaps.supportedTransforms == 0)
        {
            addTreeItem(flagsItem, "none", "");
        }
        else
        {
            addFlagTreeItem(flagsItem, "IDENTITY_BIT_KHR", (VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR & surfaceCaps.supportedTransforms));
            addFlagTreeItem(flagsItem, "ROTATE_90_BIT_KHR", (VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR & surfaceCaps.supportedTransforms));
            addFlagTreeItem(flagsItem, "ROTATE_180_BIT_KHR", (VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR & surfaceCaps.supportedTransforms));
            addFlagTreeItem(flagsItem, "ROTATE_270_BIT_KHR", (VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR & surfaceCaps.supportedTransforms));
            addFlagTreeItem(flagsItem, "HORIZONTAL_MIRROR_BIT_KHR", (VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_BIT_KHR & surfaceCaps.supportedTransforms));
            addFlagTreeItem(flagsItem, "HORIZONTAL_MIRROR_ROTATE_90_BIT_KHR", (VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_90_BIT_KHR & surfaceCaps.supportedTransforms));
            addFlagTreeItem(flagsItem, "HORIZONTAL_MIRROR_ROTATE_180_BIT_KHR", (VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_180_BIT_KHR & surfaceCaps.supportedTransforms));
            addFlagTreeItem(flagsItem, "HORIZONTAL_MIRROR_ROTATE_270_BIT_KHR", (VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_270_BIT_KHR & surfaceCaps.supportedTransforms));
            addFlagTreeItem(flagsItem, "INHERIT_BIT_KHR", (VK_SURFACE_TRANSFORM_INHERIT_BIT_KHR & surfaceCaps.supportedTransforms));
        }

        // Composite alpha
        flagsItem = addTreeItem(surfaceCapsItem, "Composite alpha flags", "");
        if (surfaceCaps.supportedCompositeAlpha == 0)
        {
            addTreeItem(flagsItem, "none", "");
        }
        else
        {
            addFlagTreeItem(flagsItem, "OPAQUE_BIT", (VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR & surfaceCaps.supportedCompositeAlpha));
            addFlagTreeItem(flagsItem, "PRE_MULTIPLIED_BIT", (VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR & surfaceCaps.supportedCompositeAlpha));
            addFlagTreeItem(flagsItem, "POST_MULTIPLIED_BIT", (VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR & surfaceCaps.supportedCompositeAlpha));
            addFlagTreeItem(flagsItem, "INHERIT_BIT", (VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR & surfaceCaps.supportedCompositeAlpha));
        }

    }


    // Surface modes
    // (VKAPI_PTR *PFN_vkGetPhysicalDeviceSurfacePresentModesKHR)(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pPresentModeCount, VkPresentModeKHR* pPresentModes);
    uint32_t presentModeCount;
    QTreeWidgetItem *modesItem = addTreeItem(treeWidget->invisibleRootItem(), "Present modes", "");
    if (vkGetPhysicalDeviceSurfacePresentModesKHR(device->device, vkSurface, &presentModeCount, nullptr) == VK_SUCCESS)
    {
        if (presentModeCount > 0)
        {
            std::vector<VkPresentModeKHR> presentModes;
            presentModes.resize(presentModeCount);
            if (vkGetPhysicalDeviceSurfacePresentModesKHR(device->device, vkSurface, &presentModeCount, presentModes.data()) == VK_SUCCESS)
            {
                for (auto presentMode : presentModes)
                {
                    addTreeItem(modesItem, vulkanResources::presentModeKHRString(presentMode), "");
                }
            }
        }
        else
        {
            addTreeItem(modesItem, "none", "");
        }
    }

    // Surface formats
    uint32_t surfaceFormatCount;
    QTreeWidgetItem *formatsItem = addTreeItem(treeWidget->invisibleRootItem(), "Surface formats", "");
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(device->device, vkSurface, &surfaceFormatCount, nullptr) == VK_SUCCESS)
    {
        if (surfaceFormatCount > 0)
        {
            std::vector<VkSurfaceFormatKHR> surfaceFormats;
            surfaceFormats.resize(surfaceFormatCount);
            if (vkGetPhysicalDeviceSurfaceFormatsKHR(device->device, vkSurface, &surfaceFormatCount, surfaceFormats.data()) == VK_SUCCESS)
            {
                uint32_t index = 0;
                for (auto surfaceFormat : surfaceFormats)
                {
                    QTreeWidgetItem *formatItem = addTreeItem(formatsItem, std::to_string(index), "");
                    addTreeItem(formatItem, "Format", (vulkanResources::formatString(surfaceFormat.format)));
                    addTreeItem(formatItem, "Color space", (vulkanResources::colorSpaceKHRString(surfaceFormat.colorSpace)));
                    index++;
                }
            }
        }
        else
        {
            addTreeItem(formatsItem, "none", "");
        }
    }

    // move to function

    for (int i = 0; i < treeWidget->columnCount(); i++)
    {
        treeWidget->header()->setSectionResizeMode(i, QHeaderView::ResizeToContents);
    }

}

/// <summary>
///	Initialize vulkan and dome some initial setup
/// </summary>
bool vulkanCapsViewer::initVulkan()
{	
	VkResult vkRes;


    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "VulkanCapsViewer";
    appInfo.applicationVersion = 1;
    appInfo.pEngineName = "VulkanCapsViewer";
    appInfo.engineVersion = 1;
    appInfo.apiVersion = VK_API_VERSION_1_0;

	// Create Vulkan instance
    VkInstanceCreateInfo instanceCreateInfo = {};
    instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceCreateInfo.pApplicationInfo = &appInfo;

    // Get globalally available layers
	uint32_t layerCount = 0;
	std::vector<VkLayerProperties> globalLayerProperties;
	do 
	{
		vkRes = vkEnumerateInstanceLayerProperties(&layerCount, NULL);
		globalLayerProperties.resize(layerCount);
		if (layerCount > 0)
		{
			vkRes = vkEnumerateInstanceLayerProperties(&layerCount, &globalLayerProperties.front());
		}
	} while (vkRes == VK_INCOMPLETE);
	assert(!vkRes);

	for (auto& globalLayerProperty : globalLayerProperties) 
	{
		VulkanLayerInfo layer;
		layer.properties = globalLayerProperty;
		instanceInfo.globalLayers.push_back(layer);
	}

    std::vector<const char*> enabledExtensions = { VK_KHR_SURFACE_EXTENSION_NAME };

    // Platform specific surface extensions
#if defined(_WIN32)
    enabledExtensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif defined(__ANDROID__)
    enabledExtensions.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
#elif defined(__linux__)
    enabledExtensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
    // todo : wayland etc.
#endif

    instanceCreateInfo.enabledExtensionCount = (uint32_t)enabledExtensions.size();
    instanceCreateInfo.ppEnabledExtensionNames = enabledExtensions.data();

	// Global extensions
	getGlobalExtensions();

	// Create vulkan Instance
    vkRes = vkCreateInstance(&instanceCreateInfo, nullptr, &vkInstance);
	if (vkRes != VK_SUCCESS) 
	{
		QString error;
		if (vkRes == VK_ERROR_INCOMPATIBLE_DRIVER)
		{
			error = "No compatible Vulkan driver found!\nThis version requires a Vulkan driver that is compatible with API Level " + QString::fromStdString(vulkanResources::versionToString(VK_API_VERSION));
		}
		else
		{
			error = "Could not create Vulkan instance!\nError: " + QString::fromStdString(vulkanResources::resultString(vkRes));
		}
		QMessageBox::warning(this, tr("Error"), error);
		return false;
	}

#ifdef ANDROID
    loadVulkanFunctions(vkInstance);
#endif

    // surface properties (wip)

#if defined(_WIN32)
    VkWin32SurfaceCreateInfoKHR surfaceCreateInfo = {};
    surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surfaceCreateInfo.hinstance =  (HINSTANCE)::GetModuleHandle(NULL);
    surfaceCreateInfo.hwnd = (HWND)this->winId();;
    vkRes = vkCreateWin32SurfaceKHR(vkInstance, &surfaceCreateInfo, nullptr, &vkSurface);
#elif defined(__ANDROID__)
    VkAndroidSurfaceCreateInfoKHR surfaceCreateInfo = {};
    // todo : find a way of gettig the nativewindow from the Qt window (it's sadly not straight-forward)
    //surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    //surfaceCreateInfo.window = reinterpret_cast<ANativeWindow*>(win);
    //vkRes = vkCreateAndroidSurfaceKHR(vkInstance, &surfaceCreateInfo, NULL, &vkSurface);
#endif
    displayGlobalLayers(ui.treeWidgetGlobalLayers);
    displayGlobalExtensions();

	return true;
}

/// <summary>
///	Get details for the specificed vulkan GPU
/// </summary>
void vulkanCapsViewer::getGPUinfo(VulkanDeviceInfo *GPU, uint32_t id, VkPhysicalDevice device)
{
	VkResult vkRes;

	GPU->id = id;
	GPU->device = device;
	GPU->readLayers();
	GPU->readExtensions();
	GPU->readQueues();
	GPU->readPhyiscalProperties();
	GPU->readPhyiscalFeatures();
	GPU->readPhyiscalLimits();
	GPU->readPhyiscalMemoryProperties();

	// Request all available queues
	std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
	for (uint32_t i = 0; i < GPU->queues.size(); ++i)
	{
        float queuePriorities[1] = { 0.0f };
		VkDeviceQueueCreateInfo queueCreateInfo = {};
		queueCreateInfo.queueFamilyIndex = i;
		queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = queuePriorities;
        queueCreateInfos.push_back(queueCreateInfo);
	}

	// Init device
	VkDeviceCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	info.pNext = NULL;
	info.pQueueCreateInfos = queueCreateInfos.data();
    info.queueCreateInfoCount = (uint32_t)queueCreateInfos.size();
	info.pEnabledFeatures = NULL;
	info.ppEnabledLayerNames = NULL;
    info.enabledLayerCount = 0;

	vkRes = vkCreateDevice(GPU->device, &info, nullptr, &GPU->dev);

	if (vkRes != VK_SUCCESS)
	{
		QString error = "Could not create a Vulkan device!\nError: " + QString::fromStdString(vulkanResources::resultString(vkRes));
		QMessageBox::warning(this, tr("Error"), error);
        exit(EXIT_FAILURE);
	}

	GPU->readSupportedFormats();
	GPU->os = getOperatingSystem();
	GPU->reportVersion = version;
}

/// <summary>
///	Get list of all available GPUs that support Vulkan
/// </summary>
void vulkanCapsViewer::getGPUs()
{
	VkResult vkRes;
	uint32_t numGPUs;

	// Enumerate devices
	vkRes = vkEnumeratePhysicalDevices(vkInstance, &numGPUs, NULL);
	if (vkRes != VK_SUCCESS) 
	{
        QMessageBox::warning(this, tr("Error"), "Could not enumarted device count!");
        return;
	}
	std::vector<VkPhysicalDevice> vulkanDevices;
	vulkanDevices.resize(numGPUs);

	vkRes = vkEnumeratePhysicalDevices(vkInstance, &numGPUs, &vulkanDevices.front());
	if (vkRes != VK_SUCCESS) 
	{
        QMessageBox::warning(this, tr("Error"), "Could not enumarted physical devices!");
        return;
	}

	vulkanGPUs.resize(numGPUs);

    for (uint32_t i = 0; i < numGPUs; i++)
	{
		getGPUinfo(&vulkanGPUs[i], i, vulkanDevices[i]);
	}

	ui.comboBoxGPU->clear();
	for (auto& GPU : vulkanGPUs) 
	{
        QString deviceName = QString::fromStdString("[GPU" + to_string(GPU.id) + "] " + GPU.props.deviceName);
		ui.comboBoxGPU->addItem(deviceName);
	}

    if (vulkanGPUs.size() > 0)
    {
		displayDevice(0);
	}
	else 
	{
		QMessageBox::warning(this, tr("Error"), "Could not find a GPU with Vulkan support!");
	}	
}

/// <summary>
///	Display information on given device
/// </summary>
void vulkanCapsViewer::displayDevice(int index)
{
	assert(index < vulkanGPUs.size());

	VulkanDeviceInfo device = vulkanGPUs[index];
	selectedDeviceIndex = index;
	
	displayDeviceProperties(&device);
    displayDeviceMemoryProperites(&device);
	displayDeviceLimits(&device);
	displayDeviceFeatures(&device);
	displayDeviceLayers(&device);
	displayDeviceExtensions(&device);
	displayDeviceFormats(&device);
	displayDeviceQueues(&device);

    displaySurfaceProperties(&device);

	checkReportDatabaseState();
}

void vulkanCapsViewer::displayDeviceProperties(VulkanDeviceInfo *device)
{
	QTreeWidget *treeWidget = ui.treeWidgetDeviceProperties;
    treeWidget->clear();
	QTreeWidgetItem *treeItem = treeWidget->invisibleRootItem();
	for (auto& prop : device->properties)
	{

        if (prop.first.find("residency") == 0)
        {
            QTreeWidgetItem *propItem;
            propItem = addTreeItem(treeItem, prop.first, (prop.second == "1") ? "true" : "false");
            propItem->setForeground(1, (prop.second == "1") ? QColor::fromRgb(0, 128, 0) : QColor::fromRgb(255, 0, 0));
            continue;
        }

        if (prop.first == "driverversion")
        {
            addTreeItem(treeItem, prop.first, device->getDriverVersion());
            continue;
        }

        addTreeItem(treeItem, prop.first, prop.second);
	}

    // Operating system
    stringstream ss;
    ss << device->os.name << " " << device->os.version << " (" << device->os.architecture << ")";
    addTreeItem(treeItem, "operatingsystem", ss.str());

    for (int i = 0; i < treeWidget->columnCount(); i++)
		treeWidget->header()->setSectionResizeMode(i, QHeaderView::ResizeToContents);
}

void vulkanCapsViewer::displayDeviceMemoryProperites(VulkanDeviceInfo *device)
{
	QTreeWidget *treeWidget = ui.treeWidgetDeviceMemory;
    treeWidget->clear();
    QTreeWidgetItem *memTypeItem = addTreeItem(treeWidget->invisibleRootItem(), "Memory types", to_string(device->memoryProperties.memoryTypeCount));
    for (uint32_t i = 0; i < device->memoryProperties.memoryTypeCount; i++)
	{
		QTreeWidgetItem *memTypeInfoItem = new QTreeWidgetItem(memTypeItem);
		VkMemoryType memType = device->memoryProperties.memoryTypes[i];
		memTypeInfoItem->setText(0, QString::number(i));
        addTreeItem(memTypeInfoItem, "Heap index", to_string(memType.heapIndex));
		QTreeWidgetItem *memTypeFlags = addTreeItem(memTypeInfoItem, "Property flags", "");
        addTreeItem(memTypeFlags, "DEVICE_LOCAL", (memType.propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ? "true" : "false");
        addTreeItem(memTypeFlags, "HOST_VISIBLE_BIT", (memType.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) ? "true" : "false");
        addTreeItem(memTypeFlags, "HOST_COHERENT_BIT", (memType.propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) ? "true" : "false");
        addTreeItem(memTypeFlags, "HOST_CACHED_BIT", (memType.propertyFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) ? "true" : "false");
        addTreeItem(memTypeFlags, "LAZILY_ALLOCATED_BIT", (memType.propertyFlags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT) ? "true" : "false");
        memTypeFlags->setExpanded(true);
        memTypeInfoItem->setExpanded(true);
	}
    memTypeItem->setExpanded(true);
    QTreeWidgetItem *heapTypeItem = addTreeItem(treeWidget->invisibleRootItem(), "Memory heaps", to_string(device->memoryProperties.memoryHeapCount));
    for (uint32_t i = 0; i < device->memoryProperties.memoryHeapCount; i++)
	{
		QTreeWidgetItem *memTypeInfoItem = new QTreeWidgetItem(heapTypeItem);
		VkMemoryHeap heapType = device->memoryProperties.memoryHeaps[i];
		memTypeInfoItem->setText(0, QString::number(i));
        addTreeItem(memTypeInfoItem, "Device size", to_string(heapType.size));
        QTreeWidgetItem *memTypeFlags = addTreeItem(memTypeInfoItem, "Flags", "");
        addTreeItem(memTypeFlags, "HEAP_DEVICE_LOCAL", (heapType.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) ? "true" : "false");
        memTypeFlags->setExpanded(true);
        memTypeInfoItem->setExpanded(true);
    }
    heapTypeItem->setExpanded(true);
    for (int i = 0; i < treeWidget->columnCount(); i++)
		treeWidget->header()->setSectionResizeMode(i, QHeaderView::ResizeToContents);
}

void vulkanCapsViewer::displayDeviceLimits(VulkanDeviceInfo *device)
{
    models.limits.clear();
    QStandardItem *rootItem = models.limits.invisibleRootItem();
	for (auto const &limit : device->limits)
	{
		QList<QStandardItem *> rowItems;
		rowItems << new QStandardItem(QString::fromStdString(limit.first));
		rowItems << new QStandardItem(QString::fromStdString(limit.second));
		rootItem->appendRow(rowItems);
	}
	ui.treeViewDeviceLimits->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
}

void vulkanCapsViewer::displayDeviceFeatures(VulkanDeviceInfo *device)
{
    models.features.clear();
    QStandardItem *rootItem = models.features.invisibleRootItem();
	for (auto const &feature : device->features)
	{
		QList<QStandardItem *> rowItems;
		rowItems << new QStandardItem(QString::fromStdString(feature.first));
		rowItems << new QStandardItem((feature.second) ? "true" : "false");
		rowItems[1]->setForeground((feature.second) ? QColor::fromRgb(0, 128, 0) : QColor::fromRgb(255, 0, 0));
		rootItem->appendRow(rowItems);
	}
	ui.treeViewDeviceFeatures->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
}

void vulkanCapsViewer::displayGlobalLayers(QTreeWidget *tree)
{
    tree->clear();
	for (auto& globalLayer : instanceInfo.globalLayers) 
	{
		QTreeWidgetItem *treeItem = new QTreeWidgetItem(tree);
		treeItem->setText(0, QString::fromLatin1(globalLayer.properties.layerName));
		treeItem->setText(1, QString::fromStdString(vulkanResources::versionToString(globalLayer.properties.specVersion)));
		treeItem->setText(2, QString::fromStdString(vulkanResources::versionToString(globalLayer.properties.implementationVersion)));
        treeItem->setText(3, QString::fromStdString(to_string(globalLayer.extensions.size())));
		treeItem->setText(4, globalLayer.properties.description);
		for (auto& layerExt : globalLayer.extensions)
		{
            addTreeItem(treeItem, layerExt.extensionName, to_string(layerExt.specVersion));
		}
	}
	for (int i = 0; i < tree->columnCount(); i++)
		tree->header()->setSectionResizeMode(i, QHeaderView::ResizeToContents);
}

void vulkanCapsViewer::displayDeviceLayers(VulkanDeviceInfo *device)
{
	QTreeWidget *treeWidget = ui.treeWidgetDeviceLayers;
    treeWidget->clear();
	ui.tabWidgetDevice->setTabText(5, "Layers (" + QString::number(device->getLayers().size()) + ")");
	for (auto& layer : device->getLayers())
	{
		QTreeWidgetItem *treeItem = new QTreeWidgetItem(treeWidget);
		treeItem->setText(0, QString::fromLatin1(layer.properties.layerName));
		treeItem->setText(1, QString::fromStdString(vulkanResources::versionToString(layer.properties.specVersion)));
		treeItem->setText(2, QString::fromStdString(vulkanResources::versionToString(layer.properties.implementationVersion)));
        treeItem->setText(3, QString::fromStdString(to_string(layer.extensions.size())));
		treeItem->setText(4, layer.properties.description);
		for (auto& layerExt : layer.extensions)
		{
			addTreeItem(treeItem, layerExt.extensionName, vulkanResources::versionToString(layerExt.specVersion));
		}
	}
	for (int i = 0; i < treeWidget->columnCount(); i++)
		treeWidget->header()->setSectionResizeMode(i, QHeaderView::ResizeToContents);
}

void vulkanCapsViewer::displayDeviceFormats(VulkanDeviceInfo *device)
{
    models.formats.clear();
	QStandardItem *rootItem = models.formats.invisibleRootItem();
	for (auto const &format : device->formats)
	{
		QList<QStandardItem *> rowItems;
		rowItems << new QStandardItem(QString::fromStdString(vulkanResources::formatString(format.format)));

		std::vector<VkFormatFeatureFlags> featureFlags =
		{
			format.properties.linearTilingFeatures,
			format.properties.optimalTilingFeatures,
			format.properties.bufferFeatures
		};

		uint32_t i = 1;
		for (auto& featureFlag : featureFlags)
		{
			rowItems << new QStandardItem((featureFlag != 0) ? "true" : "false");
			rowItems[i]->setForeground((featureFlag != 0) ? QColor::fromRgb(0, 128, 0) : QColor::fromRgb(255, 0, 0));
			++i;
		}

		rootItem->appendRow(rowItems);

		struct featureSet {
			std::string name;
			VkFlags flags;
		};
		std::vector<featureSet> featureSets =
		{
			{ "Linear tiling flags", format.properties.linearTilingFeatures },
			{ "Optimal tiling flags", format.properties.optimalTilingFeatures },
			{ "Buffer features flags", format.properties.bufferFeatures }
		};

		if (format.supported) 
		{
			for (auto& featureSet : featureSets)
			{			
				QList<QStandardItem *> flagItems;
				flagItems << new QStandardItem(QString::fromStdString(featureSet.name));

				if (featureSet.flags == 0)
				{
					QList<QStandardItem *> flagItem;
					flagItem << new QStandardItem("none");
					flagItems[0]->appendRow(flagItem);
				}
				else
				{
				#define ADD_FLAG(flag) \
					if (featureSet.flags & flag) \
					{ \
						QList<QStandardItem *> flagItem; \
						flagItem << new QStandardItem(#flag); \
						flagItems[0]->appendRow(flagItem); \
					} 

					ADD_FLAG(VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
					ADD_FLAG(VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
					ADD_FLAG(VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT);
					ADD_FLAG(VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT);
					ADD_FLAG(VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT);
					ADD_FLAG(VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_ATOMIC_BIT);
					ADD_FLAG(VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT);
					ADD_FLAG(VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);
					ADD_FLAG(VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT);
					ADD_FLAG(VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
					ADD_FLAG(VK_FORMAT_FEATURE_BLIT_SRC_BIT);
					ADD_FLAG(VK_FORMAT_FEATURE_BLIT_DST_BIT);
                    ADD_FLAG(VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT);
                    ADD_FLAG(VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_CUBIC_BIT_IMG);
                }

				rowItems[0]->appendRow(flagItems);

			}
		}
	}

	QStringList formatHeaders;
	formatHeaders << "Format" << "Linear" << "Optimal" << "Buffer";
	models.formats.setHorizontalHeaderLabels(formatHeaders);

	ui.treeViewFormats->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	for (int32_t i = 1; i < models.formats.columnCount(); ++i)
	{
		ui.treeViewFormats->header()->setSectionResizeMode(i, QHeaderView::Fixed);
	}
	ui.treeViewFormats->sortByColumn(0, Qt::SortOrder::AscendingOrder);
}

void vulkanCapsViewer::displayDeviceExtensions(VulkanDeviceInfo *device)
{
	QTreeWidget *treeWidget = ui.treeWidgetDeviceExtensions;
    treeWidget->clear();
	treeWidget->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ui.tabWidgetDevice->setTabText(3, "Extensions (" + QString::number(device->extensions.size()) + ")");
	for (auto& extension : device->extensions)
	{
		QTreeWidgetItem *extItem = new QTreeWidgetItem(treeWidget);
		extItem->setText(0, QString::fromStdString(extension.extensionName));
		extItem->setText(1, QString::fromStdString(vulkanResources::versionToString(extension.specVersion)));
	}
}

void vulkanCapsViewer::displayDeviceQueues(VulkanDeviceInfo *device)
{
    ui.tabWidgetDevice->setTabText(6, "Queues (" + QString::number(device->queues.size()) + ")");
	QTreeWidget *treeWidget = ui.treeWidgetQueues;
    treeWidget->clear();
	for (auto& queue : device->queues)
	{
		QTreeWidgetItem *queueItem = new QTreeWidgetItem(treeWidget);
		queueItem->setText(0, QString::fromStdString("Queue"));
		// Support flags
		QTreeWidgetItem *queueSupportItem = addTreeItem(queueItem, "Flags", "");
		addTreeItem(queueSupportItem, "GRAPHICS_BIT", (queue.queueFlags & VK_QUEUE_GRAPHICS_BIT) ? "true" : "false");
		addTreeItem(queueSupportItem, "COMPUTE_BIT", (queue.queueFlags & VK_QUEUE_COMPUTE_BIT) ? "true" : "false");
		addTreeItem(queueSupportItem, "TRANSFER_BIT", (queue.queueFlags & VK_QUEUE_TRANSFER_BIT) ? "true" : "false");
		addTreeItem(queueSupportItem, "SPARSE_BINDING_BIT", (queue.queueFlags & VK_QUEUE_SPARSE_BINDING_BIT) ? "true" : "false");
		queueSupportItem->setExpanded(true);
		// Queue properties		
        addTreeItem(queueItem, "queueCount", to_string(queue.queueCount));
        addTreeItem(queueItem, "CotimestampValidBitsunt", to_string(queue.timestampValidBits));
        addTreeItem(queueItem, "minImageTransferGranularity.width", to_string(queue.minImageTransferGranularity.width));
        addTreeItem(queueItem, "minImageTransferGranularity.height", to_string(queue.minImageTransferGranularity.height));
        addTreeItem(queueItem, "minImageTransferGranularity.depth", to_string(queue.minImageTransferGranularity.depth));
		queueItem->setExpanded(true);
	}
	for (int i = 0; i < treeWidget->columnCount(); i++)
		treeWidget->header()->setSectionResizeMode(i, QHeaderView::ResizeToContents);
}

void vulkanCapsViewer::exportReportAsJSON(std::string fileName, std::string submitter, std::string comment)
{
	VulkanDeviceInfo device = vulkanGPUs[selectedDeviceIndex];
	device.saveToJSON(fileName, submitter, comment);
}

/// <summary>
///	Display database state for the currently selected device
/// </summary>
void vulkanCapsViewer::checkReportDatabaseState()
{
	ui.labelDevicePresent->setText("<font color='#000000'>Connecting to database...</font>");
	ui.labelDevicePresent->setVisible(true);
    ui.toolButtonOnlineDevice->setEnabled(false);

	QApplication::setOverrideCursor(Qt::WaitCursor);
	if (!databaseConnection.checkServerConnection()) 
	{
		ui.labelDevicePresent->setText("<font color='#FF0000'>Could not connect to the database!\n\nPlease check your internet connection and proxy settings!</font>");
		ui.labelDevicePresent->setVisible(true);
		QApplication::restoreOverrideCursor();
		return;
	}

	if (databaseConnection.checkReportPresent(vulkanGPUs[selectedDeviceIndex])) 
	{
        ui.toolButtonOnlineDevice->setEnabled(true);
		ui.labelDevicePresent->setText("<font color='#00813e'>Device already present in database</font>");
		// Report present, check if it can be updated		
		// TODO : Update mechanics!
		/*
		int reportId = glhttp.getReportId(core.description);
		if (canUpdateReport(reportId)) {
			ui.labelDevicePresent->setText("<font color='#0000FF'>Device already present in database, but can be updated with missing values!</font>");
		}
		*/
	}
	else 
	{
		ui.labelDevicePresent->setText("<font color='#bc0003'>Device not present in database yet</font>");
	}
	ui.labelDevicePresent->setVisible(true);
	QApplication::restoreOverrideCursor();
}
