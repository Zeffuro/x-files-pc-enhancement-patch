#include "browser_text.h"
#include "platform/game_fonts.h"

namespace saves {
namespace {
BOOL CALLBACK read_language(HMODULE, LPCWSTR, LPCWSTR, WORD found, LONG_PTR context) {
    *reinterpret_cast<LANGID*>(context) = found;
    return FALSE;
}

struct NativeStrings {
    HMODULE module;
    LANGID language = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);

    explicit NativeStrings(const std::filesystem::path& game)
        : module(
              LoadLibraryExW((game / L"XFILESE.DLL").c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE)) {
        if (module) {
            EnumResourceLanguagesW(module, RT_STRING, MAKEINTRESOURCEW(1), read_language,
                                   reinterpret_cast<LONG_PTR>(&language));
        }
    }

    ~NativeStrings() {
        if (module) {
            FreeLibrary(module);
        }
    }

    std::wstring get(unsigned id) const {
        wchar_t buffer[512]{};
        const auto size = module ? LoadStringW(module, id, buffer, 512) : 0;
        return size > 0 ? std::wstring(buffer, size) : std::wstring{};
    }
};

BrowserText english() {
    return {L"Previous",
            L"Next",
            L"Name",
            L"Empty",
            L"Slot",
            L"Remove",
            L"Remove this save?",
            L"Overwrite",
            L"Overwrite existing saved game?",
            L"Load this save? Unsaved progress will be lost.",
            L"Done",
            L"Clear",
            L"Space",
            L"Backspace",
            L"Existing files",
            L"Numbered slots",
            L"Page",
            L"Saved",
            L"Save",
            L"Load",
            L"Cancel",
            L"Cannot read save",
            L"No preview",
            L"Keep playing",
            L"Load now"};
}

BrowserText french() {
    return {L"Précédente",
            L"Suivante",
            L"Nom",
            L"Vide",
            L"Emplacement",
            L"Supprimer",
            L"Supprimer cette partie ?",
            L"Écraser",
            L"Sauver à la place d'une partie déjà existante",
            L"Charger cette partie ? La progression non sauvegardée sera perdue.",
            L"Terminé",
            L"Effacer",
            L"Espace",
            L"Retour",
            L"Fichiers existants",
            L"Emplacements",
            L"Page",
            L"Partie sauvegardée",
            L"Enregistrer",
            L"Charger",
            L"Annuler",
            L"Partie illisible",
            L"Aucun aperçu",
            L"Continuer",
            L"Charger"};
}

BrowserText italian() {
    return {L"Precedente",
            L"Successiva",
            L"Nome",
            L"Vuoto",
            L"Slot",
            L"Elimina",
            L"Eliminare questa partita?",
            L"Sovrascrivi",
            L"Riscrive il gioco salvato esistente",
            L"Caricare questa partita? I progressi non salvati andranno persi.",
            L"Fine",
            L"Cancella",
            L"Spazio",
            L"Indietro",
            L"File esistenti",
            L"Slot numerati",
            L"Pagina",
            L"Partita salvata",
            L"Salva",
            L"Carica",
            L"Annulla",
            L"Partita illeggibile",
            L"Nessuna anteprima",
            L"Continua",
            L"Carica"};
}

BrowserText japanese() {
    return {L"前へ",
            L"次へ",
            L"名前",
            L"空き",
            L"スロット",
            L"削除",
            L"このセーブを削除しますか",
            L"上書き",
            L"保存されているｹﾞｰﾑを上書きしますか",
            L"このセーブをロードしますか。未保存の進行状況は失われます。",
            L"完了",
            L"消去",
            L"空白",
            L"一字消す",
            L"既存のファイル",
            L"番号付きスロット",
            L"ページ",
            L"保存しました",
            L"保存",
            L"ロード",
            L"中止",
            L"読み込めません",
            L"プレビューなし",
            L"続ける",
            L"ロード"};
}

BrowserText german() {
    return {L"Zurück",
            L"Weiter",
            L"Name",
            L"Leer",
            L"Platz",
            L"Entfernen",
            L"Diesen Spielstand löschen?",
            L"Überschreiben",
            L"Spielstand überschreiben?",
            L"Diesen Spielstand laden? Ungesicherter Fortschritt geht verloren.",
            L"Fertig",
            L"Löschen",
            L"Leerzeichen",
            L"Rücktaste",
            L"Vorhandene Dateien",
            L"Nummerierte Plätze",
            L"Seite",
            L"Gespeichert",
            L"Speichern",
            L"Laden",
            L"Abbrechen",
            L"Spielstand unlesbar",
            L"Keine Vorschau",
            L"Weiterspielen",
            L"Jetzt laden"};
}

BrowserText spanish() {
    return {L"Anterior",
            L"Siguiente",
            L"Nombre",
            L"Vacío",
            L"Ranura",
            L"Eliminar",
            L"¿Eliminar esta partida?",
            L"Sobrescribir",
            L"¿Sobrescribir la partida guardada?",
            L"¿Cargar esta partida? Se perderá el progreso sin guardar.",
            L"Listo",
            L"Borrar",
            L"Espacio",
            L"Retroceso",
            L"Archivos existentes",
            L"Ranuras numeradas",
            L"Página",
            L"Partida guardada",
            L"Guardar",
            L"Cargar",
            L"Cancelar",
            L"No se puede leer",
            L"Sin vista previa",
            L"Continuar",
            L"Cargar ahora"};
}
}

BrowserText load_browser_text(const std::filesystem::path& game) {
    const NativeStrings native(game);
    BrowserText result;
    switch (PRIMARYLANGID(native.language)) {
        case LANG_FRENCH:
            result = french();
            break;
        case LANG_ITALIAN:
            result = italian();
            break;
        case LANG_JAPANESE:
            result = japanese();
            break;
        case LANG_GERMAN:
            result = german();
            break;
        case LANG_SPANISH:
            result = spanish();
            break;
        default:
            result = english();
            break;
    }
    if (const auto prompt = native.get(31); !prompt.empty()) {
        result.overwrite_prompt = prompt;
    }
    return result;
}

HFONT create_browser_font(const std::filesystem::path& game, int height) {
    const NativeStrings native(game);
    const bool japanese = PRIMARYLANGID(native.language) == LANG_JAPANESE;
    const bool custom = !japanese && platform::register_private_font(game / L"DLG.TTR");
    const wchar_t* face = japanese ? L"MS Gothic" : custom ? L"Schmutz ICG Cleaned" : L"Arial";
    return CreateFontW(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                       japanese ? SHIFTJIS_CHARSET : DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, face);
}
}
