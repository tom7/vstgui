// This file is part of VSTGUI. It is subject to the license terms
// in the LICENSE file found in the top-level directory of this
// distribution and at http://github.com/steinbergmedia/vstgui/LICENSE

#include "commands.h"
#include "highscorelist.h"
#include "minefieldviewcontroller.h"
#include "vstgui/lib/cdatabrowser.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/idatabrowserdelegate.h"
#include "vstgui/standalone/include/helpers/appdelegate.h"
#include "vstgui/standalone/include/helpers/menubuilder.h"
#include "vstgui/standalone/include/helpers/preferences.h"
#include "vstgui/standalone/include/helpers/uidesc/customization.h"
#include "vstgui/standalone/include/helpers/uidesc/modelbinding.h"
#include "vstgui/standalone/include/helpers/value.h"
#include "vstgui/standalone/include/helpers/windowcontroller.h"
#include "vstgui/standalone/include/helpers/windowlistener.h"
#include "vstgui/standalone/include/iapplication.h"
#include "vstgui/standalone/include/icommondirectories.h"
#include "vstgui/standalone/include/iuidescwindow.h"
#include "vstgui/uidescription/delegationcontroller.h"
#include "vstgui/uidescription/iuidescription.h"
#include "vstgui/uidescription/uiattributes.h"
#include <cassert>
#include <unordered_map>
#include <vector>

//------------------------------------------------------------------------
namespace VSTGUI {
namespace Standalone {
namespace Minesweeper {

//------------------------------------------------------------------------
class HighScores
{
public:
	static HighScores& instance ()
	{
		static HighScores gInstance;
		return gInstance;
	}

	std::shared_ptr<HighScoreList> get (uint32_t rows, uint32_t cols, uint32_t mines)
	{
		auto path = IApplication::instance ().getCommonDirectories ().get (
		    CommonDirectoryLocation::AppPreferencesPath, {}, true);
		if (!path)
			return nullptr;
		*path += getHighscoreListName (rows, cols, mines);

		auto it = lists.find (path->getString ());
		if (it != lists.end ())
		{
			return it->second;
		}

		auto list = HighScoreList::make (*path);
		lists.insert ({path->getString (), list});
		return list;
	}

private:
	UTF8String getHighscoreListName (uint32_t rows, uint32_t cols, uint32_t mines)
	{
		return toString (rows) + "x" + toString (cols) + "x" + toString (mines) + ".highscore";
	}

	std::unordered_map<std::string, std::shared_ptr<HighScoreList>> lists;
};

//------------------------------------------------------------------------
class HighScoreViewController : public DelegationController,
                                public DataBrowserDelegateAdapter,
                                public NonAtomicReferenceCounted
{
public:
	HighScoreViewController (const std::shared_ptr<HighScoreList>& list, IController* parent)
	: DelegationController (parent), list (list)
	{
	}

	int32_t dbGetNumRows (CDataBrowser* browser) override { return HighScoreListModel::Size; }
	int32_t dbGetNumColumns (CDataBrowser* browser) override { return 4; };
	CCoord dbGetRowHeight (CDataBrowser* browser) override { return 15; }
	CCoord dbGetCurrentColumnWidth (int32_t index, CDataBrowser* browser) override
	{
		switch (index)
		{
			case 0: return 20;
			case 1: return 40;
			case 2: return 100;
			case 3: return 140;
		}
		return 10;
	}
	void dbDrawCell (CDrawContext* context, const CRect& size, int32_t row, int32_t column,
	                 int32_t flags, CDataBrowser* browser) override
	{
		auto entry = list->get ().begin ();
		std::advance (entry, row);
		if (entry == list->get ().end ())
			return;
		bool valid = entry->valid ();
		switch (column)
		{
			case 0:
			{
				context->drawString (toString (row + 1), size);
				break;
			}
			case 1:
			{
				if (valid)
					context->drawString (toString (entry->seconds), size);
				break;
			}
			case 2:
			{
				if (valid)
					context->drawString (entry->name, size);
				break;
			}
			case 3:
			{
				if (valid)
				{
					char mbstr[100];
					if (std::strftime (mbstr, sizeof (mbstr), "%F", std::localtime (&entry->date)))
					{
						context->drawString (mbstr, size);
					}
				}
				break;
			}
		}
	}

	CView* createView (const UIAttributes& attributes, const IUIDescription* description) override
	{
		const auto attr = attributes.getAttributeValue (IUIDescription::kCustomViewName);
		if (attr && *attr == "DataBrowser")
		{
			return new CDataBrowser ({}, this, 0, 0.);
		}
		return nullptr;
	}

private:
	std::shared_ptr<HighScoreList> list;
};

//------------------------------------------------------------------------
void ShowHighscoreWindow (const std::shared_ptr<HighScoreList>& list)
{
	auto customization = UIDesc::Customization::make ();
	customization->addCreateViewControllerFunc (
	    "DataBrowserController",
	    [list] (const UTF8StringView& name, IController* parent, const IUIDescription* uiDesc)
	        -> IController* { return new HighScoreViewController (list, parent); });
	UIDesc::Config config;
	config.uiDescFileName = "Highscore.uidesc";
	config.viewName = "Window";
	config.customization = customization;
	config.windowConfig.title = "Minesweeper - Highscore";
	config.windowConfig.autoSaveFrameName = "MinesweeperHighscoreWindow";
	config.windowConfig.style.border ().close ().centered ();
	if (auto window = UIDesc::makeWindow (config))
		window->show ();
}

//------------------------------------------------------------------------
class NewHighScoreViewController : public DelegationController,
                                   public ValueListenerAdapter,
                                   public NonAtomicReferenceCounted
{
public:
	NewHighScoreViewController (IValue& nameValue, IValue& okValue, IController* parent)
	: DelegationController (parent), nameValue (nameValue), okValue (okValue)
	{
		okValue.registerListener (this);
		okValue.setActive (false);
	}

	CView* verifyView (CView* view, const UIAttributes& attributes,
	                   const IUIDescription* description) override
	{
		const auto attr = attributes.getAttributeValue (IUIDescription::kCustomViewName);
		if (attr)
		{
			if (*attr == "MainView")
			{
				mainView = view;
				mainView->setAlphaValue (0.f);
				mainView->setMouseEnabled (false);
			}
		}

		return DelegationController::verifyView (view, attributes, description);
	}

	void onNewHighscore (uint32_t secondsToWin, const std::shared_ptr<HighScoreList>& list)
	{
		currentSecondsToWin = secondsToWin;
		highscoreList = list;
		mainView->setAlphaValue (1.f);
		mainView->setMouseEnabled (true);
		okValue.setActive (true);
	}

	void onEndEdit (IValue& value) override
	{
		if (value.getValue () > 0.5)
		{
			value.performEdit (0.);
			auto name = Value::currentStringValue (nameValue);
			highscoreList->addHighscore (name, currentSecondsToWin);
			ShowHighscoreWindow (highscoreList);
			highscoreList = nullptr;
			mainView->setMouseEnabled (false);
			mainView->setAlphaValue (0.f);
		}
	}

private:
	SharedPointer<CView> mainView;
	IValue& nameValue;
	IValue& okValue;
	std::shared_ptr<HighScoreList> highscoreList;
	uint32_t currentSecondsToWin {0};
};

//------------------------------------------------------------------------
class DigitsDisplayConverter : public IValueConverter
{
public:
	DigitsDisplayConverter (ValueConverterPtr converter, uint32_t digits)
	: converter (converter), digits (digits)
	{
	}

	UTF8String valueAsString (IValue::Type value) const override
	{
		auto plain = static_cast<uint32_t> (std::round (converter->normalizedToPlain (value)));
		UTF8String str;
		auto val = static_cast<uint32_t> (std::pow (10, digits - 1));
		for (auto i = digits; i > 0; --i)
		{
			auto v = plain / val;
			str += toString (v);
			plain -= v * val;
			val /= 10;
		}
		return str;
	}
	IValue::Type stringAsValue (const UTF8String& string) const override
	{
		return converter->stringAsValue (string);
	}
	IValue::Type plainToNormalized (IValue::Type plain) const override
	{
		return converter->plainToNormalized (plain);
	}
	IValue::Type normalizedToPlain (IValue::Type normalized) const override
	{
		return converter->normalizedToPlain (normalized);
	}

private:
	ValueConverterPtr converter;
	uint32_t digits {3};
};

//------------------------------------------------------------------------
class WindowController : public WindowControllerAdapter,
                         public UIDesc::Customization,
                         public UIDesc::IModelBinding,
                         public ICommandHandler
{
public:
	static constexpr auto valueRows = "Rows";
	static constexpr auto valueCols = "Cols";
	static constexpr auto valueMines = "Mines";
	static constexpr auto valueStart = "Start";
	static constexpr auto valueFlags = "Flags";
	static constexpr auto valueTime = "Time";
	static constexpr auto valueMouseMode = "MouseMode";
	static constexpr auto valueHighScoreOK = "HighScoreOK";
	static constexpr auto valueHighScoreName = "HighScoreName";

	WindowController ()
	{
		IApplication::instance ().registerCommand (NewGameCommand, 'n');
		IApplication::instance ().registerCommand (NewBeginnerGameCommand, 0);
		IApplication::instance ().registerCommand (NewIntermediateGameCommand, 0);
		IApplication::instance ().registerCommand (NewExpertGameCommand, 0);
		IApplication::instance ().registerCommand (MouseModeCommand, 0);
		IApplication::instance ().registerCommand (TouchpadModeCommand, 0);

		addCreateViewControllerFunc ("MinefieldController", [this] (const auto& name, auto* parent,
		                                                            auto* uidesc) {
			if (!minefieldViewController)
			{
				auto flagsValue = modelBinding.getValue (valueFlags);
				auto timeValue = modelBinding.getValue (valueTime);
				minefieldViewController = new MinefieldViewController (
				    *flagsValue, *timeValue, parent,
				    [this] (uint32_t secondsToWin) { onWon (secondsToWin); });
				if (auto valueObject = modelBinding.getValue (valueMouseMode))
				{
					minefieldViewController->setMouseMode (valueObject->getValue () >= 0.5 ? true :
					                                                                         false);
				}
			}
			minefieldViewController->remember ();
			return minefieldViewController;
		});

		addCreateViewControllerFunc (
		    "NewHighScoreViewController", [this] (const auto& name, auto* parent, auto* uidesc) {
			    if (!highscoreViewController)
			    {
				    auto nameValue = modelBinding.getValue (valueHighScoreName);
				    auto okValue = modelBinding.getValue (valueHighScoreOK);
				    highscoreViewController =
				        new NewHighScoreViewController (*nameValue, *okValue, parent);
			    }
			    highscoreViewController->remember ();
			    return highscoreViewController;
		    });

		modelBinding.addValue (
		    Value::make (valueRows, 0, Value::makeRangeConverter (8, 30, 0)),
		    UIDesc::ValueCalls::onEndEdit ([this] (auto& value) { verifyNumMines (); }));
		modelBinding.addValue (
		    Value::make (valueCols, 0, Value::makeRangeConverter (8, 30, 0)),
		    UIDesc::ValueCalls::onEndEdit ([this] (auto& value) { verifyNumMines (); }));
		modelBinding.addValue (
		    Value::make (valueMines, 0, Value::makeRangeConverter (4, 668, 0)),
		    UIDesc::ValueCalls::onEndEdit ([this] (auto& value) { verifyNumMines (); }));
		modelBinding.addValue (Value::make (
		    valueFlags, 0,
		    std::make_shared<DigitsDisplayConverter> (Value::makeRangeConverter (0, 668, 0), 2)));
		modelBinding.addValue (
		    Value::make (valueTime, 0,
		                 std::make_shared<DigitsDisplayConverter> (
		                     Value::makeRangeConverter (0, maxTimeInSeconds, 0), 3)));
		if (auto value = modelBinding.getValue (valueMines))
			Value::performSinglePlainEdit (*value.get (), 10);
		modelBinding.addValue (
		    Value::make (valueStart),
		    UIDesc::ValueCalls::onEndEdit ([this] (auto& value) { startNewGame (); }));
		modelBinding.addValue (
		    Value::make (valueMouseMode), UIDesc::ValueCalls::onPerformEdit ([this] (auto& value) {
			    if (minefieldViewController)
				    minefieldViewController->setMouseMode (value.getValue () >= 0.5 ? true : false);
		    }));

		modelBinding.addValue (Value::makeStringValue (valueHighScoreName, ""));
		modelBinding.addValue (Value::make (valueHighScoreOK));

		loadDefaults ();
	}

	void loadDefaults ()
	{
		Preferences prefs ("Values");
		if (auto value = prefs.getNumber<int32_t> (valueRows))
		{
			if (auto valueObject = modelBinding.getValue (valueRows))
				Value::performSinglePlainEdit (*valueObject, *value);
		}
		if (auto value = prefs.getNumber<int32_t> (valueCols))
		{
			if (auto valueObject = modelBinding.getValue (valueCols))
				Value::performSinglePlainEdit (*valueObject, *value);
		}
		if (auto value = prefs.getNumber<int32_t> (valueMines))
		{
			if (auto valueObject = modelBinding.getValue (valueMines))
				Value::performSinglePlainEdit (*valueObject, *value);
		}
		if (auto value = prefs.getNumber<bool> (valueMouseMode))
		{
			if (auto valueObject = modelBinding.getValue (valueMouseMode))
				Value::performSinglePlainEdit (*valueObject, *value);
		}
	}

	void storeDefaults ()
	{
		Preferences prefs ("Values");
		if (auto value = modelBinding.getValue (valueRows))
			prefs.setNumber (valueRows, static_cast<int32_t> (Value::currentPlainValue (*value)));
		if (auto value = modelBinding.getValue (valueCols))
			prefs.setNumber (valueCols, static_cast<int32_t> (Value::currentPlainValue (*value)));
		if (auto value = modelBinding.getValue (valueMines))
			prefs.setNumber (valueMines, static_cast<int32_t> (Value::currentPlainValue (*value)));
		if (auto value = modelBinding.getValue (valueMouseMode))
			prefs.setNumber (valueMouseMode, static_cast<bool> (Value::currentPlainValue (*value)));
	}

	bool canHandleCommand (const Command& command) override
	{
		if (command.group == GameGroup)
		{
			if (command.name == TouchpadMode)
			{
				if (modelBinding.getValue (valueMouseMode)->getValue () < 0.5)
					return false;
			}
			if (command.name == MouseMode)
			{
				if (modelBinding.getValue (valueMouseMode)->getValue () >= 0.5)
					return false;
			}
			return true;
		}
		return false;
	}

	bool handleCommand (const Command& command) override
	{
		if (command.group != GameGroup)
			return false;
		if (command.name == TouchpadMode)
		{
			auto value = modelBinding.getValue (valueMouseMode);
			Value::performSingleEdit (*value, 0.);
			return true;
		}
		else if (command.name == MouseMode)
		{
			auto value = modelBinding.getValue (valueMouseMode);
			Value::performSingleEdit (*value, 1.);
			return true;
		}

		auto rowValue = modelBinding.getValue (valueRows);
		auto colValue = modelBinding.getValue (valueCols);
		auto mineValue = modelBinding.getValue (valueMines);
		if (!rowValue || !colValue || !mineValue)
			return false;
		if (command == NewBeginnerGameCommand)
		{
			Value::performSinglePlainEdit (*rowValue, 9);
			Value::performSinglePlainEdit (*colValue, 9);
			Value::performSinglePlainEdit (*mineValue, 10);
		}
		else if (command == NewIntermediateGameCommand)
		{
			Value::performSinglePlainEdit (*rowValue, 16);
			Value::performSinglePlainEdit (*colValue, 16);
			Value::performSinglePlainEdit (*mineValue, 40);
		}
		else if (command == NewExpertGameCommand)
		{
			Value::performSinglePlainEdit (*rowValue, 16);
			Value::performSinglePlainEdit (*colValue, 30);
			Value::performSinglePlainEdit (*mineValue, 99);
		}
		startNewGame ();
		return true;
	}

	void beforeShow (IWindow& w) override { window = &w; }
	void onShow (const IWindow& w) override { startNewGame (); }
	void onClosed (const IWindow& w) override { storeDefaults (); }

	const ValueList& getValues () const override { return modelBinding.getValues (); }

	std::tuple<uint32_t, uint32_t, uint32_t> getRowsColsMines ()
	{
		auto rows =
		    static_cast<uint32_t> (Value::currentPlainValue (*modelBinding.getValue (valueRows)));
		auto cols =
		    static_cast<uint32_t> (Value::currentPlainValue (*modelBinding.getValue (valueCols)));
		auto mines =
		    static_cast<uint32_t> (Value::currentPlainValue (*modelBinding.getValue (valueMines)));
		return {rows, cols, mines};
	}

	void startNewGame ()
	{
		assert (minefieldViewController);
		uint32_t rows, cols, mines;
		std::tie (rows, cols, mines) = getRowsColsMines ();
		minefieldViewController->startGame (rows, cols, mines);
	}

	void verifyNumMines ()
	{
		uint32_t rows, cols, mines;
		std::tie (rows, cols, mines) = getRowsColsMines ();
		if (rows * cols < mines)
		{
			Value::performSinglePlainEdit (*modelBinding.getValue (valueMines), rows * cols * 0.8);
		}
	}

	void onWon (uint32_t secondsToWin)
	{
		uint32_t rows, cols, mines;
		std::tie (rows, cols, mines) = getRowsColsMines ();
		auto highscoreList = HighScores::instance ().get (rows, cols, mines);
		if (!highscoreList)
			return;

		auto pos = highscoreList->isHighScore (secondsToWin);
		if (pos)
		{
			highscoreViewController->onNewHighscore (secondsToWin, highscoreList);
		}
	}

	void showHighscoreListWindowDebug ()
	{
		uint32_t rows, cols, mines;
		std::tie (rows, cols, mines) = getRowsColsMines ();
		auto highscoreList = HighScores::instance ().get (rows, cols, mines);
		if (!highscoreList)
			return;
		ShowHighscoreWindow (highscoreList);
	}

private:
	UIDesc::ModelBindingCallbacks modelBinding;
	MinefieldViewController* minefieldViewController {nullptr};
	NewHighScoreViewController* highscoreViewController {nullptr};
	IWindow* window {nullptr};
};

//------------------------------------------------------------------------
class MyApplication : public Application::DelegateAdapter,
                      public WindowListenerAdapter,
                      public MenuBuilderAdapter
{
public:
	MyApplication ()
	: Application::DelegateAdapter ({"Minesweeper", "1.0.0", "vstgui.examples.minesweeper"})
	{
	}

	void finishLaunching () override
	{
		auto windowController = std::make_shared<WindowController> ();
		UIDesc::Config config;
		config.uiDescFileName = "Window.uidesc";
		config.viewName = "Window";
		config.customization = windowController;
		config.modelBinding = windowController;
		config.windowConfig.title = "Minesweeper";
		config.windowConfig.autoSaveFrameName = "MinesweeperWindow";
		config.windowConfig.style.border ().close ().centered ().size ();
		if (auto window = UIDesc::makeWindow (config))
		{
			window->show ();
			window->registerWindowListener (this);
		}
		else
		{
			IApplication::instance ().quit ();
		}
	}
	void onClosed (const IWindow& window) override { IApplication::instance ().quit (); }

	SortFunction getCommandGroupSortFunction (const Interface& context,
	                                          const UTF8String& group) const override
	{
		if (group == GameGroup)
		{
			return [] (const UTF8String& lhs, const UTF8String& rhs) {
				static const auto order = {NewGameCommand.name, NewBeginnerGameCommand.name,
				                           NewIntermediateGameCommand.name,
				                           NewExpertGameCommand.name};
				auto leftIndex = std::find (order.begin (), order.end (), lhs);
				auto rightIndex = std::find (order.begin (), order.end (), rhs);
				return std::distance (leftIndex, rightIndex) > 0;
			};
		}
		return {};
	}

	bool prependMenuSeparator (const Interface& context, const Command& cmd) const override
	{
		if (cmd == Commands::CloseWindow || cmd == MouseModeCommand)
			return true;
		return false;
	}
};

static Application::Init gAppDelegate (std::make_unique<MyApplication> ());

//------------------------------------------------------------------------
} // Minesweeper
} // Standalone
} // VSTGUI
