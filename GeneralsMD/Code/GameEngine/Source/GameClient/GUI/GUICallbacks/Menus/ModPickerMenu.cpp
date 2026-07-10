// GeneralsX @feature Claude 10/07/2026 Task 12 (D7): mod picker menu.
// Scans GameData/Mods/ for subdirectories, populates a ListBox, and writes the
// selected mod path to mod.txt on activate. Cancel pops without changes.
// Pattern: ExtrasMenu.cpp skeleton + updateNotifyButton dynamic button on MainMenu.

#include "PreRTS.h"

#include <vector>

#ifndef _WIN32
#include <dirent.h>
#include <sys/stat.h>
#endif

#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "GameClient/Shell.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/WindowLayout.h"
#include "Common/UnicodeString.h"
#include "Common/AsciiString.h"

static NameKeyType buttonActivateID = NAMEKEY_INVALID;
static NameKeyType buttonCancelID = NAMEKEY_INVALID;
static NameKeyType listBoxModsID = NAMEKEY_INVALID;

static GameWindow *modPickerListBox = nullptr;

static std::vector<AsciiString> s_modPaths;

static void scanModDirectories()
{
	s_modPaths.clear();

#ifndef _WIN32
	DIR *dir = opendir("Mods");
	if (!dir)
		return;

	struct dirent *entry;
	while ((entry = readdir(dir)) != nullptr)
	{
		if (entry->d_name[0] == '.')
			continue;

		AsciiString fullpath = AsciiString("Mods/");
		fullpath.concat(entry->d_name);
		struct stat st;
		if (stat(fullpath.str(), &st) != 0)
			continue;
		if (!S_ISDIR(st.st_mode))
			continue;

		s_modPaths.push_back(fullpath);
	}
	closedir(dir);
#endif
}

static void writeModTxt(const AsciiString &modPath)
{
	FILE *f = fopen("mod.txt", "w");
	if (f)
	{
		fprintf(f, "%s\n", modPath.str());
		fclose(f);
	}
}

void ModPickerMenuInit(WindowLayout *layout, void *userData)
{
	buttonActivateID = TheNameKeyGenerator->nameToKey("ModPickerMenu.wnd:ButtonActivate");
	buttonCancelID = TheNameKeyGenerator->nameToKey("ModPickerMenu.wnd:ButtonCancel");
	listBoxModsID = TheNameKeyGenerator->nameToKey("ModPickerMenu.wnd:ListBoxMods");

	modPickerListBox = TheWindowManager->winGetWindowFromId(nullptr, listBoxModsID);

	scanModDirectories();

	if (modPickerListBox)
	{
		GadgetListBoxReset(modPickerListBox);
		for (const auto &modPath : s_modPaths)
		{
			UnicodeString displayText;
			displayText.translate(modPath.str());
			GadgetListBoxAddEntryText(modPickerListBox, displayText, 0xFFFFFFFF, -1);
		}
	}
}

void ModPickerMenuUpdate(WindowLayout *layout, void *userData)
{
}

void ModPickerMenuShutdown(WindowLayout *layout, void *userData)
{
	modPickerListBox = nullptr;
	s_modPaths.clear();
}

WindowMsgHandledType ModPickerMenuSystem(GameWindow *window, UnsignedInt msg,
																				 WindowMsgData mData1, WindowMsgData mData2)
{
	switch (msg)
	{
		case GWM_CREATE:
			break;

		case GWM_DESTROY:
			break;

		case GWM_INPUT_FOCUS:
		{
			if (mData1 == TRUE)
				*(Bool *)mData2 = TRUE;
			return MSG_HANDLED;
		}

		case GBM_SELECTED:
		{
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();

			if (controlID == buttonActivateID)
			{
				if (modPickerListBox && !s_modPaths.empty())
				{
					Int selected = -1;
					GadgetListBoxGetSelected(modPickerListBox, &selected);
					if (selected >= 0 && selected < (Int)s_modPaths.size())
					{
						writeModTxt(s_modPaths[selected]);
					}
				}
				TheShell->pop();
			}
			else if (controlID == buttonCancelID)
			{
				TheShell->pop();
			}
			break;
		}

		default:
			break;
	}

	return MSG_IGNORED;
}

WindowMsgHandledType ModPickerMenuInput(GameWindow *window, UnsignedInt msg,
																				WindowMsgData mData1, WindowMsgData mData2)
{
	switch (msg)
	{
		case GWM_CHAR:
		{
			UnsignedByte key = mData1;
			if (key == KEY_ESC)
			{
				TheShell->pop();
				return MSG_HANDLED;
			}
			break;
		}
		default:
			break;
	}
	return MSG_IGNORED;
}
