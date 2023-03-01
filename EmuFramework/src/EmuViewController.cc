/*  This file is part of EmuFramework.

	Imagine is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	Imagine is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with EmuFramework.  If not, see <http://www.gnu.org/licenses/> */

#define LOGTAG "EmuViewController"
#include <emuframework/EmuViewController.hh>
#include <emuframework/EmuApp.hh>
#include <emuframework/EmuAppHelper.hh>
#include <emuframework/EmuSystem.hh>
#include <emuframework/EmuView.hh>
#include <emuframework/EmuVideoLayer.hh>
#include <emuframework/EmuVideo.hh>
#include <emuframework/EmuAudio.hh>
#include <emuframework/EmuMainMenuView.hh>
#include "EmuOptions.hh"
#include "WindowData.hh"
#include "configFile.hh"
#include <imagine/base/ApplicationContext.hh>
#include <imagine/base/Screen.hh>
#include <imagine/gfx/Renderer.hh>
#include <imagine/gfx/RendererTask.hh>
#include <imagine/gfx/RendererCommands.hh>
#include <imagine/gui/AlertView.hh>
#include <imagine/gui/ToastView.hh>
#include <imagine/fs/FS.hh>
#include <imagine/util/format.hh>

namespace EmuEx
{

EmuViewController::EmuViewController(ViewAttachParams viewAttach,
	VController &vCtrl, EmuVideoLayer &videoLayer, EmuSystem &sys):
	emuView{viewAttach, &videoLayer, sys},
	emuInputView{viewAttach, vCtrl, videoLayer},
	popup{viewAttach},
	viewStack{app()}
{
	emuInputView.setController(this);
	auto &win = viewAttach.window();
	auto &face = viewAttach.viewManager().defaultFace();
	auto &screen = *viewAttach.window().screen();
	popup.setFace(face);
	{
		auto viewNav = std::make_unique<BasicNavView>
		(
			viewAttach,
			&face,
			app().asset(AssetID::arrow),
			app().asset(AssetID::display)
		);
		viewNav->setRotateLeftButton(true);
		viewNav->setOnPushLeftBtn(
			[this](const Input::Event &)
			{
				viewStack.popAndShow();
			});
		viewNav->setOnPushRightBtn(
			[this](const Input::Event &)
			{
				app().showEmulation();
			});
		viewNav->showRightBtn(false);
		viewStack.setShowNavViewBackButton(viewAttach.viewManager().needsBackControl());
		app().onCustomizeNavView(*viewNav);
		viewStack.setNavView(std::move(viewNav));
	}
	viewStack.showNavView(app().showsTitleBar());
	emuView.setLayoutInputView(&inputView());
}

void EmuViewController::pushAndShowMainMenu(ViewAttachParams viewAttach, EmuVideoLayer &videoLayer,
	EmuAudio &emuAudio)
{
	auto mainMenu = EmuApp::makeView(viewAttach, EmuApp::ViewID::MAIN_MENU);
	static_cast<EmuMainMenuView*>(mainMenu.get())->setAudioVideo(emuAudio, videoLayer);
	viewStack.pushAndShow(std::move(mainMenu));
}

static bool shouldExitFromViewRootWithoutPrompt(const Input::KeyEvent &e)
{
	return e.map() == Input::Map::SYSTEM && (Config::envIsAndroid || Config::envIsLinux);
}

bool EmuMenuViewStack::inputEvent(const Input::Event &e)
{
	if(ViewStack::inputEvent(e))
	{
		return true;
	}
	if(e.keyEvent())
	{
		auto &keyEv = *e.keyEvent();
		bool hasEmuContent = app().system().hasContent();
		if(keyEv.pushed(Input::DefaultKey::CANCEL))
		{
			if(size() == 1)
			{
				//logMsg("cancel button at view stack root");
				if(keyEv.repeated())
				{
					return true;
				}
				if(hasEmuContent || (!hasEmuContent && !shouldExitFromViewRootWithoutPrompt(keyEv)))
				{
					app().showExitAlert(top().attachParams(), e);
				}
				else
				{
					app().appContext().exit();
				}
			}
			else
			{
				popAndShow();
			}
			return true;
		}
		if(keyEv.pushed() && app().viewController().isMenuDismissKey(keyEv) && !hasModalView())
		{
			app().showEmulation();
			return true;
		}
	}
	return false;
}

void EmuViewController::pushAndShow(std::unique_ptr<View> v, const Input::Event &e, bool needsNavView, bool isModal)
{
	app().showUI(false);
	viewStack.pushAndShow(std::move(v), e, needsNavView, isModal);
}

void EmuViewController::pop()
{
	viewStack.pop();
}

void EmuViewController::popTo(View &v)
{
	viewStack.popTo(v);
}

void EmuViewController::dismissView(View &v, bool refreshLayout)
{
	viewStack.dismissView(v, showingEmulation ? false : refreshLayout);
}

void EmuViewController::dismissView(int idx, bool refreshLayout)
{
	viewStack.dismissView(idx, showingEmulation ? false : refreshLayout);
}

bool EmuViewController::inputEvent(const Input::Event &e)
{
	if(showingEmulation)
	{
		return emuInputView.inputEvent(e);
	}
	return viewStack.inputEvent(e);
}

bool EmuViewController::extraWindowInputEvent(const Input::Event &e)
{
	if(showingEmulation && e.keyEvent())
	{
		return emuInputView.inputEvent(e);
	}
	return false;
}

void EmuViewController::movePopupToWindow(IG::Window &win)
{
	auto &origWin = popup.window();
	if(origWin == win)
		return;
	auto &origWinData = windowData(origWin);
	origWinData.hasPopup = false;
	auto &winData = windowData(win);
	winData.hasPopup = true;
	popup.setWindow(&win);
}

void EmuViewController::moveEmuViewToWindow(IG::Window &win)
{
	auto &origWin = emuView.window();
	if(origWin == win)
		return;
	if(showingEmulation)
	{
		win.setDrawEventPriority(origWin.setDrawEventPriority());
	}
	auto &origWinData = windowData(origWin);
	origWinData.hasEmuView = false;
	auto &winData = windowData(win);
	winData.hasEmuView = true;
	emuView.setWindow(&win);
	winData.applyViewRect(emuView);
	if(win == appContext().mainWindow())
		emuView.setLayoutInputView(&inputView());
	else
		emuView.setLayoutInputView(nullptr);
}

void EmuViewController::configureWindowForEmulation(IG::Window &win, bool running)
{
	if constexpr(Config::SCREEN_FRAME_INTERVAL)
		win.screen()->setFrameInterval(app().frameInterval());
	emuView.renderer().setWindowValidOrientations(win, running ? app().emuOrientation() : app().menuOrientation());
	win.setIntendedFrameRate(running ? app().intendedFrameRate(win) : 0.);
	movePopupToWindow(running ? emuView.window() : emuInputView.window());
}

void EmuViewController::showEmulationView()
{
	if(showingEmulation)
		return;
	viewStack.top().onHide();
	showingEmulation = true;
	configureWindowForEmulation(emuView.window(), true);
	if(emuView.window() != emuInputView.window())
		emuInputView.postDraw();
	emuInputView.resetInput();
	placeEmuViews();
	emuInputView.setSystemGestureExclusion(true);
}

void EmuViewController::showMenuView(bool updateTopView)
{
	if(!showingEmulation)
		return;
	showingEmulation = false;
	emuInputView.setSystemGestureExclusion(false);
	configureWindowForEmulation(emuView.window(), false);
	emuView.postDraw();
	if(updateTopView)
	{
		viewStack.show();
		viewStack.top().postDraw();
	}
}

void EmuViewController::placeEmuViews()
{
	emuView.place();
	emuInputView.place();
}

void EmuViewController::placeElements()
{
	//logMsg("placing app elements");
	{
		auto &winData = windowData(popup.window());
		winData.applyViewRect(popup);
		popup.place();
	}
	auto &winData = app().mainWindowData();
	emuView.manager().setTableXIndentToDefault(appContext().mainWindow());
	placeEmuViews();
	viewStack.place(winData.contentBounds(), winData.windowBounds());
}

void EmuViewController::updateMainWindowViewport(IG::Window &win, IG::Viewport viewport, Gfx::RendererTask &task)
{
	auto &winData = windowData(win);
	task.setDefaultViewport(win, viewport);
	winData.updateWindowViewport(win, viewport, task.renderer());
	if(winData.hasEmuView)
	{
		winData.applyViewRect(emuView);
	}
	winData.applyViewRect(emuInputView);
	placeElements();
}

void EmuViewController::updateExtraWindowViewport(IG::Window &win, IG::Viewport viewport, Gfx::RendererTask &task)
{
	logMsg("view resize for extra window");
	task.setDefaultViewport(win, viewport);
	auto &winData = windowData(win);
	winData.updateWindowViewport(win, viewport, task.renderer());
	winData.applyViewRect(emuView);
	emuView.place();
}

void EmuViewController::updateEmuAudioStats(int underruns, int overruns, int callbacks, double avgCallbackFrames, int frames)
{
	emuView.updateAudioStats(underruns, overruns, callbacks, avgCallbackFrames, frames);
}

void EmuViewController::clearEmuAudioStats()
{
	emuView.clearAudioStats();
}

void EmuViewController::popToSystemActionsMenu()
{
	viewStack.popTo(viewStack.viewIdx("System Actions"));
}

void EmuViewController::postDrawToEmuWindows()
{
	emuView.window().postDraw();
}

IG::Screen *EmuViewController::emuWindowScreen() const
{
	return emuView.window().screen();
}

IG::Window &EmuViewController::emuWindow() const
{
	return emuView.window();
}

WindowData &EmuViewController::emuWindowData()
{
	return windowData(emuView.window());
}

void EmuViewController::pushAndShowModal(std::unique_ptr<View> v, const Input::Event &e, bool needsNavView)
{
	pushAndShow(std::move(v), e, needsNavView, true);
}

void EmuViewController::pushAndShowModal(std::unique_ptr<View> v, bool needsNavView)
{
	auto e = v->appContext().defaultInputEvent();
	pushAndShowModal(std::move(v), e, needsNavView);
}

bool EmuViewController::hasModalView() const
{
	return viewStack.hasModalView();
}

void EmuViewController::popModalViews()
{
	viewStack.popModalViews();
}

void EmuViewController::prepareDraw()
{
	popup.prepareDraw();
	emuView.prepareDraw();
	viewStack.prepareDraw();
}

bool EmuViewController::drawMainWindow(IG::Window &win, IG::WindowDrawParams params, Gfx::RendererTask &task)
{
	return task.draw(win, params, {},
		[this](IG::Window &win, Gfx::RendererCommands &cmds)
	{
		cmds.clear();
		auto &winData = windowData(win);
		cmds.basicEffect().setModelViewProjection(cmds, Gfx::Mat4::ident(), winData.projM);
		if(showingEmulation)
		{
			if(winData.hasEmuView)
			{
				emuView.draw(cmds);
			}
			emuInputView.draw(cmds);
			if(winData.hasPopup)
				popup.draw(cmds);
		}
		else
		{
			if(winData.hasEmuView)
			{
				emuView.draw(cmds);
			}
			viewStack.draw(cmds);
			popup.draw(cmds);
		}
		cmds.present();
	});
}

bool EmuViewController::drawExtraWindow(IG::Window &win, IG::WindowDrawParams params, Gfx::RendererTask &task)
{
	return task.draw(win, params, {},
		[this](IG::Window &win, Gfx::RendererCommands &cmds)
	{
		cmds.clear();
		auto &winData = windowData(win);
		cmds.basicEffect().setModelViewProjection(cmds, Gfx::Mat4::ident(), winData.projM);
		emuView.draw(cmds);
		if(winData.hasPopup)
		{
			popup.draw(cmds);
		}
		cmds.present();
	});
}

void EmuViewController::popToRoot()
{
	viewStack.popToRoot();
}

void EmuViewController::showNavView(bool show)
{
	viewStack.showNavView(show);
}

void EmuViewController::setShowNavViewBackButton(bool show)
{
	viewStack.setShowNavViewBackButton(show);
}

void EmuViewController::showSystemActionsView(ViewAttachParams attach, const Input::Event &e)
{
	app().showUI();
	if(!viewStack.contains("System Actions"))
	{
		viewStack.pushAndShow(EmuApp::makeView(attach, EmuApp::ViewID::SYSTEM_ACTIONS), e);
	}
}

void EmuViewController::onInputDevicesChanged()
{
	if(viewStack.size() == 1) // update bluetooth items
		viewStack.top().onShow();
}

void EmuViewController::onSystemCreated()
{
	viewStack.navView()->showRightBtn(true);
}

void EmuViewController::onSystemClosed()
{
	viewStack.navView()->showRightBtn(false);
	if(int idx = viewStack.viewIdx("System Actions");
		idx > 0)
	{
		// pop to menu below System Actions
		viewStack.popTo(idx - 1);
	}
}

EmuInputView &EmuViewController::inputView()
{
	return emuInputView;
}

IG::ToastView &EmuViewController::popupMessageView()
{
	return popup;
}

EmuVideoLayer &EmuViewController::videoLayer() const
{
	return *emuView.videoLayer();
}

IG::ApplicationContext EmuViewController::appContext() const
{
	return emuWindow().appContext();
}

bool EmuViewController::isMenuDismissKey(const Input::KeyEvent &e) const
{
	using namespace IG::Input;
	Key dismissKey = Keycode::MENU;
	Key dismissKey2 = Keycode::GAME_Y;
	if(Config::MACHINE_IS_PANDORA && e.device()->subtype() == Device::Subtype::PANDORA_HANDHELD)
	{
		if(hasModalView()) // make sure not performing text input
			return false;
		dismissKey = Keycode::SPACE;
	}
	return e.key() == dismissKey || e.key() == dismissKey2;
}

void EmuViewController::onHide()
{
	viewStack.top().onHide();
}

}
