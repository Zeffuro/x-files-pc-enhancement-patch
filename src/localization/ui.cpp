#include "ui.h"

#include <array>
#include <cwchar>
#include <stdexcept>
#include <string>
#include <vector>

namespace ui {
namespace {

struct Entry {
    const wchar_t* english;
    const wchar_t* german;
    const wchar_t* french;
    const wchar_t* spanish;
    const wchar_t* italian;
    const wchar_t* japanese;
};

constexpr Entry entries[] = {
    {L"The X-Files Setup", L"The X-Files Installation", L"Installation de The X-Files",
     L"Instalación de The X-Files", L"Installazione di The X-Files", L"The X-Files セットアップ"},
    {L"Install The X-Files", L"The X-Files installieren", L"Installer The X-Files",
     L"Instalar The X-Files", L"Installa The X-Files", L"The X-Files をインストール"},
    {L"Choose your game files or disc images. Setup detects the game language.",
     L"Wählen Sie Spieldateien oder Datenträgerabbilder. Die Spielsprache wird erkannt.",
     L"Choisissez les fichiers du jeu ou des images disque. La langue du jeu est détectée.",
     L"Elige archivos del juego o imágenes de disco. Se detectará el idioma del juego.",
     L"Scegli i file di gioco o le immagini disco. La lingua del gioco viene rilevata.",
     L"ゲームファイルまたはディスクイメージを選択してください。ゲームの言語は自動検出されます。"},
    {L"Game files or disc images", L"Spieldateien oder Datenträgerabbilder",
     L"Fichiers du jeu ou images disque", L"Archivos del juego o imágenes de disco",
     L"File di gioco o immagini disco", L"ゲームファイルまたはディスクイメージ"},
    {L"Install to a new folder", L"In neuen Ordner installieren",
     L"Installer dans un nouveau dossier", L"Instalar en una carpeta nueva",
     L"Installa in una nuova cartella", L"新しいフォルダーにインストール"},
    {L"Folder...", L"Ordner...", L"Dossier...", L"Carpeta...", L"Cartella...", L"フォルダー..."},
    {L"Image...", L"Abbild...", L"Image...", L"Imagen...", L"Immagine...", L"イメージ..."},
    {L"Browse...", L"Durchsuchen...", L"Parcourir...", L"Examinar...", L"Sfoglia...", L"参照..."},
    {L"Start in a window", L"Im Fenster starten", L"Démarrer en fenêtre", L"Iniciar en una ventana",
     L"Avvia in finestra", L"ウィンドウで起動"},
    {L"Desktop shortcut", L"Desktopverknüpfung", L"Raccourci sur le bureau",
     L"Acceso directo en el escritorio", L"Collegamento sul desktop",
     L"デスクトップにショートカット"},
    {L"Start Menu shortcut", L"Startmenüverknüpfung", L"Raccourci du menu Démarrer",
     L"Acceso directo en Inicio", L"Collegamento nel menu Start",
     L"スタートメニューにショートカット"},
    {L"Updating? Close the game and extract the new release ZIP into the installed folder. Replace "
     L"files when asked. Saves and settings are kept.",
     L"Update: Schließen Sie das Spiel und entpacken Sie die neue ZIP-Datei in den "
     L"Installationsordner. Ersetzen Sie Dateien auf Nachfrage. Spielstände und Einstellungen "
     L"bleiben erhalten.",
     L"Mise à jour : fermez le jeu et extrayez le nouveau ZIP dans le dossier d'installation. "
     L"Remplacez les fichiers à la demande. Les sauvegardes et réglages sont conservés.",
     L"Para actualizar, cierra el juego y extrae el nuevo ZIP en la carpeta instalada. Reemplaza "
     L"los archivos cuando se te pida. Se conservan las partidas y los ajustes.",
     L"Per aggiornare, chiudi il gioco ed estrai il nuovo ZIP nella cartella di installazione. "
     L"Sostituisci i file quando richiesto. Salvataggi e impostazioni restano invariati.",
     L"更新するにはゲームを閉じ、新しい ZIP "
     L"をインストール先に展開してファイルを上書きしてください。セーブデータと設定は保持されます。"},
    {L"Interface language (independent of game media)",
     L"Oberflächensprache (unabhängig von den Spieldateien)",
     L"Langue de l'interface (indépendante des fichiers du jeu)",
     L"Idioma de la interfaz (independiente del disco)",
     L"Lingua dell'interfaccia (indipendente dai file di gioco)",
     L"表示言語 (ゲームディスクとは別)"},
    {L"Interface language", L"Oberflächensprache", L"Langue de l'interface",
     L"Idioma de la interfaz", L"Lingua dell'interfaccia", L"表示言語"},
    {L"Install", L"Installieren", L"Installer", L"Instalar", L"Installa", L"インストール"},
    {L"Cancel", L"Abbrechen", L"Annuler", L"Cancelar", L"Annulla", L"キャンセル"},
    {L"Play", L"Spielen", L"Jouer", L"Jugar", L"Gioca", L"プレイ"},
    {L"Close", L"Schließen", L"Fermer", L"Cerrar", L"Chiudi", L"閉じる"},
    {L"Checking game files and free space...", L"Spieldateien und freien Speicher prüfen...",
     L"Vérification des fichiers et de l'espace libre...",
     L"Comprobando archivos y espacio libre...", L"Controllo file e spazio libero...",
     L"ゲームファイルと空き容量を確認中..."},
    {L"Copying and verifying game files...", L"Spieldateien kopieren und prüfen...",
     L"Copie et vérification des fichiers du jeu...",
     L"Copiando y verificando archivos del juego...", L"Copia e verifica dei file di gioco...",
     L"ゲームファイルをコピーして確認中..."},
    {L"Ready to play. The source disc or folder is no longer needed.",
     L"Spielbereit. Der Quelldatenträger oder -ordner wird nicht mehr benötigt.",
     L"Prêt à jouer. Le disque ou dossier source n'est plus nécessaire.",
     L"Listo para jugar. Ya no necesitas el disco o la carpeta de origen.",
     L"Pronto per giocare. Il disco o la cartella di origine non è più necessario.",
     L"プレイの準備ができました。元のディスクやフォルダーはもう不要です。"},
    {L"Setup did not finish. Check the details below before retrying.",
     L"Installation nicht abgeschlossen. Prüfen Sie die Details und versuchen Sie es erneut.",
     L"L'installation a échoué. Vérifiez les détails avant de réessayer.",
     L"La instalación no terminó. Revisa los detalles antes de reintentar.",
     L"Installazione non completata. Controlla i dettagli prima di riprovare.",
     L"セットアップは完了しませんでした。詳細を確認して再試行してください。"},
    {L"Stopping after the current file...", L"Nach der aktuellen Datei anhalten...",
     L"Arrêt après le fichier en cours...", L"Deteniendo tras el archivo actual...",
     L"Arresto dopo il file corrente...", L"現在のファイルが終わったら停止します..."},
    {L"Choose a PC disc ISO or CUE sheet", L"ISO- oder CUE-Datei eines PC-Datenträgers wählen",
     L"Choisir une image ISO ou CUE du disque PC", L"Elige una imagen ISO o CUE del disco de PC",
     L"Scegli un'immagine ISO o CUE del disco PC", L"PC ディスクの ISO または CUE を選択"},
    {L"Choose game files or all seven CD images",
     L"Spieldateien oder alle sieben CD-Abbilder wählen",
     L"Choisir les fichiers du jeu ou les sept images CD",
     L"Elige archivos del juego o las siete imágenes de CD",
     L"Scegli i file di gioco o tutte le sette immagini CD",
     L"ゲームファイルまたは 7 枚の CD イメージを選択"},
    {L"Choose a parent folder for The X-Files", L"Übergeordneten Ordner für The X-Files wählen",
     L"Choisir le dossier parent de The X-Files", L"Elige la carpeta principal para The X-Files",
     L"Scegli la cartella principale per The X-Files", L"The X-Files の親フォルダーを選択"},
    {L"The X-Files enhancements", L"The X-Files Erweiterungen", L"Améliorations de The X-Files",
     L"Mejoras de The X-Files", L"Migliorie di The X-Files", L"The X-Files 拡張設定"},
    {L"Display and audio", L"Anzeige und Audio", L"Affichage et audio", L"Pantalla y audio",
     L"Schermo e audio", L"画面と音声"},
    {L"Display mode", L"Anzeigemodus", L"Mode d'affichage", L"Modo de pantalla",
     L"Modalità schermo", L"表示モード"},
    {L"Windowed", L"Fenster", L"Fenêtre", L"Ventana", L"Finestra", L"ウィンドウ"},
    {L"Borderless fullscreen", L"Randloses Vollbild", L"Plein écran sans bordure",
     L"Pantalla completa sin bordes", L"Schermo intero senza bordi", L"ボーダーレス全画面"},
    {L"Alt+Enter switches between windowed and borderless.",
     L"Alt+Eingabe wechselt zwischen Fenster und randlosem Vollbild.",
     L"Alt+Entrée alterne entre fenêtre et plein écran sans bordure.",
     L"Alt+Entrar cambia entre ventana y pantalla completa sin bordes.",
     L"Alt+Invio alterna finestra e schermo intero senza bordi.",
     L"Alt+Enter でウィンドウとボーダーレス全画面を切り替えます。"},
    {L"Window size", L"Fenstergröße", L"Taille de la fenêtre", L"Tamaño de ventana",
     L"Dimensione finestra", L"ウィンドウサイズ"},
    {L"Keep current size", L"Aktuelle Größe beibehalten", L"Conserver la taille actuelle",
     L"Mantener tamaño actual", L"Mantieni dimensione attuale", L"現在のサイズを維持"},
    {L"640 x 480 (original 4:3)", L"640 x 480 (Original 4:3)", L"640 x 480 (original 4:3)",
     L"640 x 480 (original 4:3)", L"640 x 480 (originale 4:3)", L"640 x 480 (元の 4:3)"},
    {L"1280 x 720 (16:9, side bars)", L"1280 x 720 (16:9, Seitenbalken)",
     L"1280 x 720 (16:9, bandes latérales)", L"1280 x 720 (16:9, barras laterales)",
     L"1280 x 720 (16:9, bande laterali)", L"1280 x 720 (16:9、左右に余白)"},
    {L"Scaling filter", L"Skalierungsfilter", L"Filtre de mise à l'échelle", L"Filtro de escalado",
     L"Filtro di ridimensionamento", L"拡大フィルター"},
    {L"Original proportions. Filters require Direct3D 9.",
     L"Originalproportionen. Filter benötigen Direct3D 9.",
     L"Proportions d'origine. Filtres avec Direct3D 9.",
     L"Proporciones originales. Los filtros requieren Direct3D 9.",
     L"Proporzioni originali. I filtri richiedono Direct3D 9.",
     L"元の縦横比。フィルターには Direct3D 9 が必要です。"},
    {L"Audio output", L"Audioausgabe", L"Sortie audio", L"Salida de audio", L"Uscita audio",
     L"音声出力"},
    {L"Nearest neighbour", L"Nächster Nachbar", L"Plus proche voisin", L"Vecino más cercano",
     L"Vicino più prossimo", L"最近傍"},
    {L"Bilinear (soft)", L"Bilinear (weich)", L"Bilinéaire (doux)", L"Bilineal (suave)",
     L"Bilineare (morbido)", L"バイリニア (ソフト)"},
    {L"Bicubic (default)", L"Bikubisch (Standard)", L"Bicubique (défaut)",
     L"Bicúbico (predeterminado)", L"Bicubico (predefinito)", L"バイキュービック (標準)"},
    {L"Lanczos (sharp)", L"Lanczos (scharf)", L"Lanczos (net)", L"Lanczos (nítido)",
     L"Lanczos (nitido)", L"Lanczos (シャープ)"},
    {L"Controller", L"Controller", L"Manette", L"Mando", L"Controller", L"コントローラー"},
    {L"Enable gamepad", L"Gamepad aktivieren", L"Activer la manette", L"Activar mando",
     L"Abilita controller", L"ゲームパッドを有効にする"},
    {L"Analog stick moves pointer (D-pad selects controls)",
     L"Analogstick bewegt Zeiger (Steuerkreuz wählt Bedienelemente)",
     L"Le stick déplace le pointeur (la croix choisit les commandes)",
     L"El stick mueve el puntero (la cruceta selecciona controles)",
     L"La levetta muove il puntatore (la croce seleziona i comandi)",
     L"スティックでポインター移動 (方向キーで操作を選択)"},
    {L"Spring-centred pointer (return to centre on release)",
     L"Zeiger kehrt beim Loslassen zur Mitte zurück",
     L"Retour du pointeur au centre après relâchement", L"El puntero vuelve al centro al soltarlo",
     L"Il puntatore torna al centro quando si rilascia", L"離すとポインターを中央に戻す"},
    {L"Focus highlight", L"Fokusmarkierung", L"Surbrillance de sélection",
     L"Resaltado de selección", L"Evidenziazione selezione", L"選択枠"},
    {L"Automatic", L"Automatisch", L"Automatique", L"Automático", L"Automatico", L"自動"},
    {L"Always", L"Immer", L"Toujours", L"Siempre", L"Sempre", L"常に"},
    {L"Off", L"Aus", L"Désactivé", L"Desactivado", L"Disattivato", L"オフ"},
    {L"On", L"Ein", L"Activé", L"Activado", L"Attivato", L"オン"},
    {L"Subtitles", L"Untertitel", L"Sous-titres", L"Subtítulos", L"Sottotitoli", L"字幕"},
    {L"Closed captions", L"Untertitel für Hörgeschädigte", L"Sous-titres descriptifs",
     L"Subtítulos descriptivos", L"Sottotitoli descrittivi", L"クローズドキャプション"},
    {L"Game preference", L"Spieleinstellung", L"Préférence du jeu", L"Preferencia del juego",
     L"Preferenza del gioco", L"ゲームの設定"},
    {L"Font", L"Schriftart", L"Police", L"Fuente", L"Carattere", L"フォント"},
    {L"Size (%)", L"Größe (%)", L"Taille (%)", L"Tamaño (%)", L"Dimensione (%)", L"サイズ (%)"},
    {L"Dark background", L"Dunkler Hintergrund", L"Fond sombre", L"Fondo oscuro", L"Sfondo scuro",
     L"暗い背景"},
    {L"Black", L"Schwarz", L"Noir", L"Negro", L"Nero", L"黒"},
    {L"Charcoal", L"Anthrazit", L"Gris anthracite", L"Gris carbón", L"Antracite", L"チャコール"},
    {L"Navy", L"Marineblau", L"Bleu marine", L"Azul marino", L"Blu navy", L"ネイビー"},
    {L"Opacity", L"Deckkraft", L"Opacité", L"Opacidad", L"Opacità", L"不透明度"},
    {L"Game", L"Spiel", L"Jeu", L"Juego", L"Gioco", L"ゲーム"},
    {L"Restore black menu backgrounds", L"Schwarze Menühintergründe wiederherstellen",
     L"Restaurer les fonds noirs des menus", L"Restaurar fondos negros de los menús",
     L"Ripristina gli sfondi neri dei menu", L"メニューの黒い背景を復元"},
    {L"Skip workstation login puzzle", L"Computer-Anmelderätsel überspringen",
     L"Passer l'énigme de connexion", L"Omitir el puzle de inicio de sesión",
     L"Salta l'enigma di accesso", L"端末のログインパズルをスキップ"},
    {L"Skip menu logo and entrance animations", L"Menülogo und Startanimationen überspringen",
     L"Passer le logo et les animations du menu", L"Omitir logo y animaciones del menú",
     L"Salta logo e animazioni del menu", L"メニューのロゴと開始アニメをスキップ"},
    {L"Save browser with thumbnails", L"Spielstandliste mit Vorschaubildern",
     L"Liste des sauvegardes avec miniatures", L"Lista de partidas con miniaturas",
     L"Elenco salvataggi con anteprime", L"サムネイル付きセーブ一覧"},
    {L"Tools...", L"Werkzeuge...", L"Outils...", L"Herramientas...", L"Strumenti...", L"ツール..."},
    {L"The X-Files tools", L"The X-Files Werkzeuge", L"Outils The X-Files",
     L"Herramientas de The X-Files", L"Strumenti di The X-Files", L"The X-Files ツール"},
    {L"Troubleshooting", L"Fehlerbehebung", L"Dépannage", L"Solución de problemas",
     L"Risoluzione problemi", L"トラブルシューティング"},
    {L"Save logs and version details to a ZIP for a bug report. You can include a saved game. "
     L"Nothing is uploaded.",
     L"Logs und Versionsdaten als ZIP für einen Fehlerbericht speichern. Ein Spielstand kann "
     L"beigefügt werden. Es wird nichts hochgeladen.",
     L"Enregistrez les journaux et la version dans un ZIP pour signaler un bug. Vous pouvez "
     L"inclure une sauvegarde. Aucun envoi automatique.",
     L"Guarda registros y versión en un ZIP para informar de un fallo. Puedes incluir una partida. "
     L"No se sube nada.",
     L"Salva log e versione in uno ZIP per segnalare un problema. Puoi includere un salvataggio. "
     L"Non viene caricato nulla.",
     L"不具合報告用にログとバージョン情報を ZIP "
     L"に保存します。セーブデータも含められます。自動送信はしません。"},
    {L"Include a saved game...", L"Spielstand beifügen...", L"Inclure une sauvegarde...",
     L"Incluir una partida...", L"Includi un salvataggio...", L"セーブデータを含める..."},
    {L"Open logs folder", L"Logordner öffnen", L"Ouvrir le dossier des journaux",
     L"Abrir carpeta de registros", L"Apri cartella dei log", L"ログフォルダーを開く"},
    {L"Save report ZIP...", L"Berichts-ZIP speichern...", L"Enregistrer le ZIP du rapport...",
     L"Guardar informe ZIP...", L"Salva ZIP del rapporto...", L"報告用 ZIP を保存..."},
    {L"Saved games", L"Spielstände", L"Sauvegardes", L"Partidas guardadas", L"Salvataggi",
     L"セーブデータ"},
    {L"Load during exploration or from the main menu. Loading discards unsaved progress. Save "
     L"during exploration using a new filename.",
     L"Beim Erkunden oder im Hauptmenü laden. Ungespeicherter Fortschritt geht verloren. Beim "
     L"Erkunden unter neuem Dateinamen speichern.",
     L"Chargez en exploration ou depuis le menu principal. Le chargement perd la progression non "
     L"sauvegardée. Enregistrez sous un nouveau nom.",
     L"Carga al explorar o desde el menú principal. Se perderá el progreso sin guardar. Guarda al "
     L"explorar con un nombre nuevo.",
     L"Carica durante l'esplorazione o dal menu principale. I progressi non salvati andranno "
     L"persi. Salva con un nuovo nome.",
     L"探索中またはメインメニューから読み込めます。未保存の進行は失われます。探索中は新しい名前で保"
     L"存してください。"},
    {L"Load from file...", L"Aus Datei laden...", L"Charger un fichier...",
     L"Cargar desde archivo...", L"Carica da file...", L"ファイルから読み込む..."},
    {L"Save to file...", L"In Datei speichern...", L"Enregistrer dans un fichier...",
     L"Guardar en archivo...", L"Salva su file...", L"ファイルに保存..."},
    {L"Export movie captions as editable SRT files, or install a subtitle pack. Original movies "
     L"aren't changed.",
     L"Filmuntertitel als bearbeitbare SRT-Dateien exportieren oder ein Untertitelpaket "
     L"installieren. Originalfilme bleiben unverändert.",
     L"Exportez les sous-titres en fichiers SRT modifiables ou installez un pack. Les vidéos "
     L"d'origine restent intactes.",
     L"Exporta subtítulos en archivos SRT editables o instala un paquete. Los vídeos originales no "
     L"cambian.",
     L"Esporta sottotitoli in file SRT modificabili o installa un pacchetto. I filmati originali "
     L"non cambiano.",
     L"字幕を編集可能な SRT "
     L"ファイルに書き出すか、字幕パックをインストールできます。元の動画は変更されません。"},
    {L"Export subtitles...", L"Untertitel exportieren...", L"Exporter les sous-titres...",
     L"Exportar subtítulos...", L"Esporta sottotitoli...", L"字幕を書き出す..."},
    {L"Install subtitles...", L"Untertitel installieren...", L"Installer des sous-titres...",
     L"Instalar subtítulos...", L"Installa sottotitoli...", L"字幕をインストール..."},
    {L"Developer tools", L"Entwicklerwerkzeuge", L"Outils de développement",
     L"Herramientas de desarrollo", L"Strumenti di sviluppo", L"開発者ツール"},
    {L"Preview live clips and add your own labels and notes.",
     L"Aktuelle Clips ansehen und eigene Namen und Notizen hinzufügen.",
     L"Prévisualisez les extraits et ajoutez des noms et notes.",
     L"Previsualiza clips y añade nombres y notas.", L"Visualizza clip e aggiungi nomi e note.",
     L"クリップを確認し、名前やメモを追加できます。"},
    {L"Open developer tools...", L"Entwicklerwerkzeuge öffnen...",
     L"Ouvrir les outils de développement...", L"Abrir herramientas de desarrollo...",
     L"Apri strumenti di sviluppo...", L"開発者ツールを開く..."},
    {L"Ctrl+F11 opens it during play.", L"Strg+F11 öffnet sie während des Spiels.",
     L"Ctrl+F11 les ouvre pendant le jeu.", L"Ctrl+F11 las abre durante el juego.",
     L"Ctrl+F11 li apre durante il gioco.", L"プレイ中は Ctrl+F11 で開きます。"},
    {L"About...", L"Über...", L"À propos...", L"Acerca de...", L"Informazioni...",
     L"バージョン情報..."},
    {L"ZIP file...", L"ZIP-Datei...", L"Fichier ZIP...", L"Archivo ZIP...", L"File ZIP...",
     L"ZIP ファイル..."},
    {L"Make yourself at home", L"Machen Sie es sich bequem", L"Installez-vous", L"Ponte cómodo",
     L"Mettiti comodo", L"快適にプレイしましょう"},
    {L"Choose how you want to play. You can change these options later with F10.",
     L"Wählen Sie Ihre Spielweise. Mit F10 können Sie diese Optionen später ändern.",
     L"Choisissez votre façon de jouer. Vous pourrez modifier ces options plus tard avec F10.",
     L"Elige cómo quieres jugar. Puedes cambiar estas opciones después con F10.",
     L"Scegli come giocare. Puoi modificare queste opzioni in seguito con F10.",
     L"プレイ方法を選んでください。これらの設定は後で F10 から変更できます。"},
    {L"Recommended", L"Empfohlen", L"Recommandé", L"Recomendado", L"Consigliato", L"おすすめ"},
    {L"Original game", L"Originalspiel", L"Jeu d'origine", L"Juego original", L"Gioco originale",
     L"オリジナル版"},
    {L"Start game", L"Spiel starten", L"Lancer le jeu", L"Iniciar juego", L"Avvia gioco",
     L"ゲーム開始"},
    {L"Not now", L"Jetzt nicht", L"Pas maintenant", L"Ahora no", L"Non ora", L"後で"},
    {L"Cannot save your choices. Please try again.",
     L"Ihre Auswahl konnte nicht gespeichert werden. Bitte versuchen Sie es erneut.",
     L"Impossible d'enregistrer vos choix. Veuillez réessayer.",
     L"No se pudieron guardar tus opciones. Inténtalo de nuevo.",
     L"Impossibile salvare le scelte. Riprova.",
     L"選択を保存できませんでした。もう一度お試しください。"},
    {L"Saved game exported.", L"Spielstand exportiert.", L"Sauvegarde exportée.",
     L"Partida exportada.", L"Salvataggio esportato.", L"セーブデータを書き出しました。"},
    {L"Install subtitles", L"Untertitel installieren", L"Installer des sous-titres",
     L"Instalar subtítulos", L"Installa sottotitoli", L"字幕をインストール"},
    {L"Export subtitles", L"Untertitel exportieren", L"Exporter les sous-titres",
     L"Exportar subtítulos", L"Esporta sottotitoli", L"字幕を書き出す"},
    {L"Open a ZIP pack or its extracted folder.",
     L"Öffnen Sie ein ZIP-Paket oder den entpackten Ordner.",
     L"Ouvrez un pack ZIP ou son dossier extrait.", L"Abre un paquete ZIP o su carpeta extraída.",
     L"Apri un pacchetto ZIP o la cartella estratta.",
     L"ZIP パックまたは展開済みフォルダーを開いてください。"},
    {L"Use a ZIP to share subtitles, or a folder to edit them.",
     L"Verwenden Sie ein ZIP zum Teilen oder einen Ordner zum Bearbeiten.",
     L"Utilisez un ZIP pour partager ou un dossier pour modifier.",
     L"Usa un ZIP para compartir o una carpeta para editar.",
     L"Usa uno ZIP per condividere o una cartella per modificare.",
     L"共有するには ZIP、編集するにはフォルダーを選んでください。"},
    {L"Install subtitle pack", L"Untertitelpaket installieren", L"Installer un pack de sous-titres",
     L"Instalar paquete de subtítulos", L"Installa pacchetto sottotitoli",
     L"字幕パックをインストール"},
    {L"Export subtitle pack", L"Untertitelpaket exportieren", L"Exporter un pack de sous-titres",
     L"Exportar paquete de subtítulos", L"Esporta pacchetto sottotitoli", L"字幕パックを書き出す"},
    {L"Choose the subtitle pack folder", L"Ordner des Untertitelpakets wählen",
     L"Choisir le dossier du pack de sous-titres", L"Elige la carpeta del paquete de subtítulos",
     L"Scegli la cartella del pacchetto sottotitoli", L"字幕パックのフォルダーを選択"},
    {L"Choose an empty folder for subtitles", L"Leeren Ordner für Untertitel wählen",
     L"Choisir un dossier vide pour les sous-titres",
     L"Elige una carpeta vacía para los subtítulos", L"Scegli una cartella vuota per i sottotitoli",
     L"字幕用の空フォルダーを選択"},
    {L"Checking movies...", L"Filme prüfen...", L"Vérification des vidéos...",
     L"Comprobando vídeos...", L"Controllo filmati...", L"動画を確認中..."},
    {L"Processed: ", L"Verarbeitet: ", L"Traités : ", L"Procesados: ", L"Elaborati: ",
     L"処理済み: "},
    {L" of ", L" von ", L" sur ", L" de ", L" di ", L" / "},
    {L"Cancelling...", L"Abbrechen...", L"Annulation...", L"Cancelando...", L"Annullamento...",
     L"キャンセル中..."},
    {L"Subtitles installed. Set closed captions to On to show them.",
     L"Untertitel installiert. Aktivieren Sie Untertitel für Hörgeschädigte, um sie anzuzeigen.",
     L"Sous-titres installés. Activez les sous-titres descriptifs pour les afficher.",
     L"Subtítulos instalados. Activa los subtítulos descriptivos para verlos.",
     L"Sottotitoli installati. Attiva i sottotitoli descrittivi per mostrarli.",
     L"字幕をインストールしました。表示するにはクローズドキャプションをオンにしてください。"},
    {L"Subtitles exported as SRT files. Edit the text, then use Install subtitles to load the "
     L"pack.",
     L"Untertitel als SRT-Dateien exportiert. Bearbeiten Sie den Text und laden Sie ihn mit "
     L"Untertitel installieren.",
     L"Sous-titres exportés en fichiers SRT. Modifiez-les, puis utilisez Installer des "
     L"sous-titres.",
     L"Subtítulos exportados como archivos SRT. Edita el texto y usa Instalar subtítulos para "
     L"cargarlos.",
     L"Sottotitoli esportati come file SRT. Modifica il testo, poi usa Installa sottotitoli.",
     L"字幕を SRT ファイルに書き出しました。編集後、字幕のインストールから読み込んでください。"},
    {L"\n\nSkipped ", L"\n\nÜbersprungen: ", L"\n\nIgnorées : ", L"\n\nOmitidos: ",
     L"\n\nIgnorati: ", L"\n\nスキップ: "},
    {L" unreadable movies. See skipped.txt in the exported pack.",
     L" nicht lesbare Filme. Details in skipped.txt im Exportpaket.",
     L" vidéos illisibles. Voir skipped.txt dans le pack exporté.",
     L" vídeos ilegibles. Consulta skipped.txt en el paquete exportado.",
     L" filmati illeggibili. Vedi skipped.txt nel pacchetto esportato.",
     L" 本の読み取れない動画。書き出したパックの skipped.txt を参照してください。"},
    {L"The X-Files subtitles", L"The X-Files Untertitel", L"Sous-titres The X-Files",
     L"Subtítulos de The X-Files", L"Sottotitoli di The X-Files", L"The X-Files 字幕"},
    {L"Save troubleshooting report", L"Fehlerbericht speichern",
     L"Enregistrer le rapport de dépannage", L"Guardar informe de diagnóstico",
     L"Salva rapporto diagnostico", L"診断レポートを保存"},
    {L"Save game to file", L"Spielstand in Datei speichern",
     L"Enregistrer la partie dans un fichier", L"Guardar partida en archivo",
     L"Salva partita su file", L"セーブデータをファイルに保存"},
    {L"Choose saved game", L"Spielstand wählen", L"Choisir une sauvegarde",
     L"Elegir partida guardada", L"Scegli un salvataggio", L"セーブデータを選択"},
    {L"Report saved. Nothing was uploaded. Review the ZIP before sharing it.",
     L"Bericht gespeichert. Es wurde nichts hochgeladen. Prüfen Sie das ZIP vor dem Teilen.",
     L"Rapport enregistré. Aucun envoi. Vérifiez le ZIP avant de le partager.",
     L"Informe guardado. No se subió nada. Revisa el ZIP antes de compartirlo.",
     L"Rapporto salvato. Non è stato caricato nulla. Controlla lo ZIP prima di condividerlo.",
     L"レポートを保存しました。送信はしていません。共有前に ZIP を確認してください。"},
    {L"The game stopped unexpectedly. Diagnostic logs have been kept in the game's logs "
     L"folder.\n\nCreate a troubleshooting ZIP now?",
     L"Das Spiel wurde unerwartet beendet. Diagnoseprotokolle liegen im Logordner des "
     L"Spiels.\n\nJetzt ein ZIP für die Fehlerbehebung erstellen?",
     L"Le jeu s'est arrêté de façon inattendue. Les journaux sont dans le dossier des logs du "
     L"jeu.\n\nCréer un ZIP de diagnostic maintenant ?",
     L"El juego se cerró inesperadamente. Los registros están en la carpeta de logs del "
     L"juego.\n\n¿Crear un ZIP de diagnóstico ahora?",
     L"Il gioco si è chiuso inaspettatamente. I log sono nella cartella del gioco.\n\nCreare ora "
     L"uno ZIP diagnostico?",
     L"ゲームが予期せず終了しました。診断ログはゲームのログフォルダーに保存されています。\n\n診断用"
     L" ZIP を作成しますか？"},
    {L"Include a saved game to help reproduce the problem?\n\nYou can choose one existing save. "
     L"The game cannot recover unsaved progress after a crash.",
     L"Einen Spielstand zur Fehleranalyse beifügen?\n\nSie können einen vorhandenen Spielstand "
     L"wählen. Ungespeicherter Fortschritt kann nach einem Absturz nicht wiederhergestellt werden.",
     L"Inclure une sauvegarde pour reproduire le problème ?\n\nVous pouvez choisir une sauvegarde "
     L"existante. La progression non sauvegardée est perdue après un plantage.",
     L"¿Incluir una partida para reproducir el problema?\n\nPuedes elegir una partida existente. "
     L"El progreso sin guardar se pierde tras un fallo.",
     L"Includere un salvataggio per riprodurre il problema?\n\nPuoi scegliere un salvataggio "
     L"esistente. I progressi non salvati non si recuperano dopo un arresto anomalo.",
     L"問題の再現に使うセーブデータを含めますか？\n\n既存のセーブを 1 "
     L"つ選べます。異常終了後に未保存の進行は復元できません。"},
    {L"Troubleshooting report", L"Fehlerbericht", L"Rapport de dépannage",
     L"Informe de diagnóstico", L"Rapporto diagnostico", L"診断レポート"},
    {L"About the patch", L"Über den Patch", L"À propos du patch", L"Acerca del parche",
     L"Informazioni sulla patch", L"パッチについて"},
    {L"Copyright (c) 2026 Zeffuro. MIT License.\n\nUses FFmpeg libraries under LGPL-2.1-or-later, "
     L"cnc-ddraw (MIT), and zlib (zlib license).\n\nLicense notices are in the game folder. "
     L"Matching FFmpeg source and the build script are in the patch release ZIP.\n\nUnofficial "
     L"patch. Game content belongs to its owners.",
     L"Copyright (c) 2026 Zeffuro. MIT-Lizenz.\n\nVerwendet FFmpeg-Bibliotheken unter "
     L"LGPL-2.1-or-later, cnc-ddraw (MIT) und zlib (zlib-Lizenz).\n\nLizenzhinweise liegen im "
     L"Spielordner. Passender FFmpeg-Quellcode und das Buildskript liegen im "
     L"Release-ZIP.\n\nInoffizieller Patch. Spielinhalte gehören ihren Eigentümern.",
     L"Copyright (c) 2026 Zeffuro. Licence MIT.\n\nUtilise les bibliothèques FFmpeg sous "
     L"LGPL-2.1-or-later, cnc-ddraw (MIT) et zlib (licence zlib).\n\nLes notices de licence sont "
     L"dans le dossier du jeu. Le code source FFmpeg correspondant et le script de compilation "
     L"sont dans le ZIP de la version.\n\nPatch non officiel. Le contenu du jeu appartient à ses "
     L"ayants droit.",
     L"Copyright (c) 2026 Zeffuro. Licencia MIT.\n\nUsa las bibliotecas FFmpeg bajo "
     L"LGPL-2.1-or-later, cnc-ddraw (MIT) y zlib (licencia zlib).\n\nLos avisos de licencia están "
     L"en la carpeta del juego. El código FFmpeg correspondiente y el script de compilación están "
     L"en el ZIP de la versión.\n\nParche no oficial. El contenido del juego pertenece a sus "
     L"titulares.",
     L"Copyright (c) 2026 Zeffuro. Licenza MIT.\n\nUsa le librerie FFmpeg con licenza "
     L"LGPL-2.1-or-later, cnc-ddraw (MIT) e zlib (licenza zlib).\n\nLe note sulle licenze sono "
     L"nella cartella del gioco. Il sorgente FFmpeg corrispondente e lo script di build sono nello "
     L"ZIP della versione.\n\nPatch non ufficiale. I contenuti del gioco appartengono ai "
     L"rispettivi proprietari.",
     L"Copyright (c) 2026 Zeffuro。MIT ライセンス。\n\nFFmpeg ライブラリ "
     L"(LGPL-2.1-or-later)、cnc-ddraw (MIT)、zlib (zlib ライセンス) "
     L"を使用します。\n\nライセンス通知はゲームフォルダーにあります。対応する FFmpeg "
     L"ソースとビルドスクリプトはリリース ZIP "
     L"にあります。\n\n非公式パッチです。ゲームのコンテンツの権利は各権利者に帰属します。"},
};

constexpr std::array<const wchar_t*, 6> codes{L"en", L"de", L"fr", L"es", L"it", L"ja"};
constexpr std::array<const wchar_t*, 6> names{L"English", L"Deutsch",  L"Français",
                                              L"Español", L"Italiano", L"日本語"};

std::filesystem::path settings_path() {
    std::vector<wchar_t> executable(32768);
    const auto length =
        GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (!length || length >= executable.size()) {
        return {};
    }
    return std::filesystem::path(executable.data()).parent_path() / L"patch.ini";
}

BOOL CALLBACK translate_child(HWND window, LPARAM parameter) {
    wchar_t class_name[32]{};
    GetClassNameW(window, class_name, _countof(class_name));
    if (std::wcscmp(class_name, L"Static") != 0 && std::wcscmp(class_name, L"Button") != 0) {
        return TRUE;
    }
    const auto selected = static_cast<Language>(parameter);
    std::wstring source(GetWindowTextLengthW(window) + 1, L'\0');
    source.resize(GetWindowTextW(window, source.data(), static_cast<int>(source.size())));
    if (!source.empty()) {
        const auto* translated = translate(source.c_str(), selected);
        if (translated != source.c_str()) {
            SetWindowTextW(window, translated);
        }
    }
    return TRUE;
}

}

Language system_language() {
    switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
        case LANG_GERMAN:
            return Language::German;
        case LANG_FRENCH:
            return Language::French;
        case LANG_SPANISH:
            return Language::Spanish;
        case LANG_ITALIAN:
            return Language::Italian;
        case LANG_JAPANESE:
            return Language::Japanese;
        default:
            return Language::English;
    }
}

Language language() {
    const auto path = settings_path();
    if (path.empty()) {
        return system_language();
    }
    return read_language(path);
}

Language read_language(const std::filesystem::path& path) {
    wchar_t code[16]{};
    GetPrivateProfileStringW(L"Interface", L"Language", L"", code, _countof(code), path.c_str());
    for (std::size_t index = 0; index < codes.size(); ++index) {
        if (_wcsicmp(code, codes[index]) == 0) {
            return static_cast<Language>(index);
        }
    }
    return system_language();
}

void save_language(const std::filesystem::path& settings_file, Language value) {
    const auto index = static_cast<std::size_t>(value);
    if (index >= codes.size() || !WritePrivateProfileStringW(L"Interface", L"Language",
                                                             codes[index], settings_file.c_str())) {
        throw std::runtime_error("Cannot save the interface language");
    }
}

const wchar_t* language_name(Language value) {
    const auto index = static_cast<std::size_t>(value);
    return index < names.size() ? names[index] : names[0];
}

const wchar_t* translate(const wchar_t* english, Language value) {
    if (!english || value == Language::English) {
        return english;
    }
    for (const auto& entry : entries) {
        if (std::wcscmp(english, entry.english) == 0) {
            switch (value) {
                case Language::German:
                    return entry.german;
                case Language::French:
                    return entry.french;
                case Language::Spanish:
                    return entry.spanish;
                case Language::Italian:
                    return entry.italian;
                case Language::Japanese:
                    return entry.japanese;
                default:
                    return english;
            }
        }
    }
    return english;
}

const wchar_t* translate(const wchar_t* english) {
    return translate(english, language());
}

void translate_dialog(HWND window, Language value) {
    std::wstring title(GetWindowTextLengthW(window) + 1, L'\0');
    title.resize(GetWindowTextW(window, title.data(), static_cast<int>(title.size())));
    if (!title.empty()) {
        SetWindowTextW(window, translate(title.c_str(), value));
    }
    EnumChildWindows(window, translate_child, static_cast<LPARAM>(value));
}

void translate_dialog(HWND window) {
    translate_dialog(window, language());
}

DialogTranslator::DialogTranslator(HWND window) : window_(window) {
    title_.resize(GetWindowTextLengthW(window) + 1, L'\0');
    title_.resize(GetWindowTextW(window, title_.data(), static_cast<int>(title_.size())));

    struct Capture {
        static BOOL CALLBACK run(HWND child, LPARAM context) {
            wchar_t class_name[32]{};
            GetClassNameW(child, class_name, _countof(class_name));
            if (std::wcscmp(class_name, L"Static") != 0 &&
                std::wcscmp(class_name, L"Button") != 0) {
                return TRUE;
            }
            std::wstring label(GetWindowTextLengthW(child) + 1, L'\0');
            label.resize(GetWindowTextW(child, label.data(), static_cast<int>(label.size())));
            if (!label.empty()) {
                auto& labels =
                    *reinterpret_cast<std::vector<std::pair<HWND, std::wstring>>*>(context);
                labels.emplace_back(child, std::move(label));
            }
            return TRUE;
        }
    };

    EnumChildWindows(window, Capture::run, reinterpret_cast<LPARAM>(&labels_));
}

void DialogTranslator::apply(Language value) const {
    SetWindowTextW(window_, translate(title_.c_str(), value));
    for (const auto& [child, english] : labels_) {
        SetWindowTextW(child, translate(english.c_str(), value));
    }
}

}
