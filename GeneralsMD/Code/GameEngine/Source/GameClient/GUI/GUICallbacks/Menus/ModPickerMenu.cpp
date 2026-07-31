// GeneralsX @feature Claude 10/07/2026 Task 12 (D7): mod picker menu.
// Scans GameData/Mods/ for subdirectories, populates a ListBox, and writes the
// selected mod path to mod.txt on activate. Cancel pops without changes.
// Pattern: ExtrasMenu.cpp skeleton + updateNotifyButton dynamic button on MainMenu.

#include "PreRTS.h"

#include <vector>

#ifndef _WIN32
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>  // unlink, rmdir (POSIX recursive delete — no std::filesystem, Scudo hazard)
#endif

// GeneralsX @feature Claude 30/07/2026 Android Mods picker: bridge the dynamic
// "Import Folder" button to GameActivity's SAF importer via SDL3's JNI accessors.
// z_gameengine has no SDL3 include path (only the Main target links SDL3), so we
// forward-declare the two C accessors we use; they resolve at link from libSDL3.
// <jni.h> ships in the NDK sysroot. Not built on non-Android targets.
#if defined(__ANDROID__)
#include <jni.h>
extern "C" {
void *SDL_GetAndroidJNIEnv(void);
void *SDL_GetAndroidActivity(void);
}
#endif

#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "GameClient/Shell.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/MessageBox.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Display.h"  // GeneralsX @bugfix: scale picker font to TheDisplay->getWidth()
#include "Common/UnicodeString.h"
#include "Common/AsciiString.h"

static NameKeyType buttonActivateID = NAMEKEY_INVALID;
static NameKeyType buttonCancelID = NAMEKEY_INVALID;
static NameKeyType listBoxModsID = NAMEKEY_INVALID;

// GeneralsX @feature Claude 30/07/2026 Android import button (created in Init,
// matched by pointer in System like the Mods button on MainMenu). ID is a unique
// name key so the window is identifiable alongside the WND-defined controls.
static NameKeyType buttonImportID = NAMEKEY_INVALID;
static GameWindow *importButton = nullptr;

// GeneralsX @feature Claude 31/07/2026 Delete button (uniform row alongside Import/Activate/Cancel).
static NameKeyType buttonDeleteID = NAMEKEY_INVALID;
static GameWindow *deleteButton = nullptr;

static GameWindow *modPickerListBox = nullptr;

// GeneralsX @feature Claude 31/07/2026 Per-mod metadata for the picker list: recursive folder
// size (POSIX, not std::filesystem — Scudo heap-corruption hazard) and active-mod flag.
struct ModEntry
{
	AsciiString path;   // "Mods/<name>" (relative)
	AsciiString name;
	long long   sizeBytes;
	Bool        isActive;
};
static std::vector<ModEntry> s_mods;
static int s_pendingDeleteIdx = -1;

static long long computeDirSizeRecursive(const char *path)
{
	long long total = 0;
	DIR *dir = opendir(path);
	if (!dir)
		return 0;
	struct dirent *entry;
	while ((entry = readdir(dir)) != nullptr)
	{
		if (entry->d_name[0] == '.' &&
			(entry->d_name[1] == '\0' || (entry->d_name[1] == '.' && entry->d_name[2] == '\0')))
			continue;  // skip "." and ".."
		char child[1024];
		snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
		struct stat st;
		if (stat(child, &st) != 0)
			continue;
		if (S_ISDIR(st.st_mode))
			total += computeDirSizeRecursive(child);
		else
			total += (long long)st.st_size;
	}
	closedir(dir);
	return total;
}

static Bool deleteDirRecursive(const char *path)
{
	DIR *dir = opendir(path);
	if (!dir)
		return FALSE;
	struct dirent *entry;
	while ((entry = readdir(dir)) != nullptr)
	{
		if (entry->d_name[0] == '.' &&
			(entry->d_name[1] == '\0' || (entry->d_name[1] == '.' && entry->d_name[2] == '\0')))
			continue;
		char child[1024];
		snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
		struct stat st;
		if (stat(child, &st) != 0)
			continue;
		if (S_ISDIR(st.st_mode))
			deleteDirRecursive(child);
		else
			unlink(child);
	}
	closedir(dir);
	return rmdir(path) == 0;
}

static void scanModDirectories()
{
	s_mods.clear();

#ifndef _WIN32
	DIR *dir = opendir("Mods");
	if (!dir)
		return;

	struct dirent *entry;
	while ((entry = readdir(dir)) != nullptr)
	{
		if (entry->d_name[0] == '.')
			continue;

		char fullpath[1024];
		snprintf(fullpath, sizeof(fullpath), "Mods/%s", entry->d_name);
		struct stat st;
		if (stat(fullpath, &st) != 0)
			continue;
		if (!S_ISDIR(st.st_mode))
			continue;

		ModEntry m;
		m.path = fullpath;
		m.name = entry->d_name;
		m.sizeBytes = computeDirSizeRecursive(fullpath);
		// parseMod stores m_modDir with a trailing separator for dir mods, so match both forms.
		AsciiString pathWithSlash = m.path;
		pathWithSlash.concat('/');
		m.isActive = TheGlobalData->m_modDir.endsWithNoCase(m.path) ||
		             TheGlobalData->m_modDir.endsWithNoCase(pathWithSlash);
		s_mods.push_back(m);
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

static void populateModListBox()
{
	if (!modPickerListBox)
		return;

	GadgetListBoxReset(modPickerListBox);
	for (const auto &m : s_mods)
	{
		char line[512];
		const char *activeTag = m.isActive ? "  [ACTIVE]" : "";
		if (m.sizeBytes >= 1048576LL)
			snprintf(line, sizeof(line), "%s  (%.1f MB)%s", m.name.str(), (double)m.sizeBytes / 1048576.0, activeTag);
		else if (m.sizeBytes >= 1024LL)
			snprintf(line, sizeof(line), "%s  (%.1f KB)%s", m.name.str(), (double)m.sizeBytes / 1024.0, activeTag);
		else
			snprintf(line, sizeof(line), "%s  (%lld B)%s", m.name.str(), (long long)m.sizeBytes, activeTag);

		UnicodeString displayText;
		displayText.translate(line);
		GadgetListBoxAddEntryText(modPickerListBox, displayText, 0xFFFFFFFF, -1);
	}
}

// GeneralsX @feature Claude 31/07/2026 MessageBoxYesNo callbacks take no args, so the pending
// delete index is stashed in s_pendingDeleteIdx by the Delete handler and consumed here.
static void confirmDeleteModYes()
{
	if (s_pendingDeleteIdx >= 0 && s_pendingDeleteIdx < (int)s_mods.size())
		deleteDirRecursive(s_mods[s_pendingDeleteIdx].path.str());
	s_pendingDeleteIdx = -1;
	scanModDirectories();
	populateModListBox();
}

static void confirmDeleteModNo()
{
	s_pendingDeleteIdx = -1;
}

#if defined(__ANDROID__)
// GeneralsX @feature Claude 30/07/2026 JNI bridge to GameActivity. Mirrors the
// SDL3Main.cpp mod-Intent pattern: get the env/activity from SDL3, look up the
// method, call it, clear any pending exception, release the local class ref.
// The activity ref is owned by SDL and must not be deleted. Any failure returns
// silently without blocking the game.
static void requestModFolderImportJni()
{
	JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
	if (!env)
		return;
	jobject activity = (jobject)SDL_GetAndroidActivity();
	if (!activity)
		return;
	jclass cls = env->GetObjectClass(activity);
	if (!cls)
	{
		if (env->ExceptionCheck())
			env->ExceptionClear();
		return;
	}
	jmethodID mid = env->GetMethodID(cls, "requestModFolderImport", "()V");
	if (!mid || env->ExceptionCheck())
	{
		if (env->ExceptionCheck())
			env->ExceptionClear();
		env->DeleteLocalRef(cls);
		return;
	}
	env->CallVoidMethod(activity, mid);
	if (env->ExceptionCheck())
		env->ExceptionClear();
	env->DeleteLocalRef(cls);
}

static bool consumeModImportedJni()
{
	JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
	if (!env)
		return false;
	jobject activity = (jobject)SDL_GetAndroidActivity();
	if (!activity)
		return false;
	jclass cls = env->GetObjectClass(activity);
	if (!cls)
	{
		if (env->ExceptionCheck())
			env->ExceptionClear();
		return false;
	}
	jmethodID mid = env->GetMethodID(cls, "consumeModImported", "()Z");
	if (!mid || env->ExceptionCheck())
	{
		if (env->ExceptionCheck())
			env->ExceptionClear();
		env->DeleteLocalRef(cls);
		return false;
	}
	jboolean result = env->CallBooleanMethod(activity, mid);
	if (env->ExceptionCheck())
	{
		env->ExceptionClear();
		env->DeleteLocalRef(cls);
		return false;
	}
	env->DeleteLocalRef(cls);
	return result == JNI_TRUE;
}
#endif

void ModPickerMenuInit(WindowLayout *layout, void *userData)
{
	buttonActivateID = TheNameKeyGenerator->nameToKey("ModPickerMenu.wnd:ButtonActivate");
	buttonCancelID = TheNameKeyGenerator->nameToKey("ModPickerMenu.wnd:ButtonCancel");
	listBoxModsID = TheNameKeyGenerator->nameToKey("ModPickerMenu.wnd:ListBoxMods");

	modPickerListBox = TheWindowManager->winGetWindowFromId(nullptr, listBoxModsID);

	// GeneralsX @bugfix Claude 30/07/2026 Set literal captions directly on the
	// listbox and buttons. The WND parser routes TEXT through GameText; these
	// keys are absent from the Android string table, so the picker showed
	// "MISSING:" placeholders instead of Mods/Activate/Cancel.
	GameWindow *activateButton = TheWindowManager->winGetWindowFromId(nullptr, buttonActivateID);
	GameWindow *cancelButton = TheWindowManager->winGetWindowFromId(nullptr, buttonCancelID);
	if (activateButton)
		GadgetButtonSetText(activateButton, UnicodeString(L"Activate"));
	if (cancelButton)
		GadgetButtonSetText(cancelButton, UnicodeString(L"Cancel"));
	if (modPickerListBox)
		// GeneralsX @tweak Claude 31/07/2026 Clear listbox text: a non-empty label makes
		// GadgetListBoxInput's GWM_LEFT_UP add a fontHeight offset to the row hit-test, so taps
		// landed below the entries. The "MODS" title is now the LabelTitle STATICTEXT instead.
		modPickerListBox->winSetText(UnicodeString(L""));

	// GeneralsX @bugfix Claude 30/07/2026 Scale the picker font to display width.
	// ModPickerMenu.wnd rectangles are authored in an 800px design coordinate
	// system and the window manager scales them to the real display, but font
	// point sizes do not scale with geometry. On a 1904px Android display the
	// WND's 18pt Arial renders ~18px tall and is unreadable. Recompute the
	// 18pt@800px base against the current display width (clamped to >=18) and
	// assign it to the listbox and Activate/Cancel/Import controls. Menu-local:
	// does not alter global font scaling or WND font values.
	const int pickerBasePointSize = 18;
	const UnsignedInt pickerDesignWidth = 800;
	int pickerPointSize = pickerBasePointSize;
	const UnsignedInt pickerDisplayWidth = TheDisplay->getWidth();
	if (pickerDisplayWidth > 0)
	{
		const double pickerScale = (double)pickerDisplayWidth / (double)pickerDesignWidth;
		pickerPointSize = (int)(pickerScale * (double)pickerBasePointSize + 0.5);
		if (pickerPointSize < pickerBasePointSize)
			pickerPointSize = pickerBasePointSize;
	}
	GameFont *pickerFont = TheWindowManager->winFindFont("Arial", pickerPointSize, FALSE);
	if (pickerFont)
	{
		if (modPickerListBox)
			modPickerListBox->winSetFont(pickerFont);
		if (activateButton)
			activateButton->winSetFont(pickerFont);
		if (cancelButton)
			cancelButton->winSetFont(pickerFont);
	}

	scanModDirectories();
	populateModListBox();

	// GeneralsX @feature Claude 31/07/2026 Import + Delete buttons live in the WND (design-
	// scaled like Activate/Cancel) so the row renders uniformly. Retrieve by ID, apply the
	// scaled font + caption. Import only functions on Android (SAF) — hide it elsewhere.
	buttonImportID = TheNameKeyGenerator->nameToKey("ModPickerMenu.wnd:ButtonImport");
	buttonDeleteID = TheNameKeyGenerator->nameToKey("ModPickerMenu.wnd:ButtonDelete");
	importButton = TheWindowManager->winGetWindowFromId(nullptr, buttonImportID);
	deleteButton = TheWindowManager->winGetWindowFromId(nullptr, buttonDeleteID);

	GameFont *rowFont = TheWindowManager->winFindFont("Arial", pickerPointSize, FALSE);
	if (importButton)
	{
		if (rowFont)
			importButton->winSetFont(rowFont);
		GadgetButtonSetText(importButton, UnicodeString(L"Import"));
#if !defined(__ANDROID__)
		importButton->winHide(TRUE);
#endif
	}
	if (deleteButton)
	{
		if (rowFont)
			deleteButton->winSetFont(rowFont);
		GadgetButtonSetText(deleteButton, UnicodeString(L"Delete"));
	}
}

void ModPickerMenuUpdate(WindowLayout *layout, void *userData)
{
#if defined(__ANDROID__)
	// GeneralsX @feature Claude 30/07/2026 If GameActivity finished a SAF import,
	// rescan Mods/ and refresh the visible list in place. Selection/apply logic
	// is untouched; the player still picks a row and taps Activate as before.
	const bool imported = consumeModImportedJni();
	if (imported)
	{
		scanModDirectories();
		populateModListBox();
	}
#endif
}

void ModPickerMenuShutdown(WindowLayout *layout, void *userData)
{
	// Import/Delete are WND-defined (layout-owned) — cleared here, freed by the layout teardown.
	importButton = nullptr;
	deleteButton = nullptr;
	buttonImportID = NAMEKEY_INVALID;
	buttonDeleteID = NAMEKEY_INVALID;
	s_pendingDeleteIdx = -1;

	modPickerListBox = nullptr;
	s_mods.clear();
	layout->hide(TRUE);
	TheShell->shutdownComplete(layout);
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
				if (modPickerListBox && !s_mods.empty())
				{
					Int selected = -1;
					GadgetListBoxGetSelected(modPickerListBox, &selected);
					if (selected >= 0 && selected < (Int)s_mods.size())
					{
						writeModTxt(s_mods[selected].path);
					}
				}
				TheShell->pop();
			}
			else if (controlID == buttonCancelID)
			{
				TheShell->pop();
			}
#if defined(__ANDROID__)
		// GeneralsX @feature Claude 30/07/2026 Import button is matched by
		// pointer (like the Mods button on MainMenu); routing it only fires
		// the SAF request and leaves Activate/Cancel exactly as before.
			else if (control == importButton)
			{
				requestModFolderImportJni();
			}
#endif
			// GeneralsX @feature Claude 31/07/2026 Delete: match by pointer. Block the active
			// mod (informative Ok box); otherwise stash the selection and confirm via Yes/No.
			else if (control == deleteButton)
			{
				Int selected = -1;
				if (modPickerListBox)
					GadgetListBoxGetSelected(modPickerListBox, &selected);
				if (selected >= 0 && selected < (Int)s_mods.size())
				{
					const ModEntry &m = s_mods[selected];
					if (m.isActive)
					{
						MessageBoxOk(UnicodeString(L"Cannot Delete"),
							UnicodeString(L"Cannot delete the active mod. Deactivate it first."), nullptr);
					}
					else
					{
						s_pendingDeleteIdx = selected;
						char body[512];
						snprintf(body, sizeof(body), "Permanently delete \"%s\"?", m.name.str());
						UnicodeString bodyText;
						bodyText.translate(body);
						MessageBoxYesNo(UnicodeString(L"Delete Mod"), bodyText,
							confirmDeleteModYes, confirmDeleteModNo);
					}
				}
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
