import QtQuick 2.0
import com.github.catsout.wallpaperEngineKde 1.2

// File access for the QML side, on top of the C++ FileHelper
Item {
    id: root

    // Internal C++ FileHelper instance
    FileHelper {
        id: fileHelper
    }

    // Promise-like wrapper for synchronous calls
    function _makePromise(value) {
        return {
            result: value,
            then: function(callback) {
                const res = callback(value);
                // Return chainable promise
                return _makePromise(res !== undefined ? res : value);
            },
            catch: function(callback) {
                // No-op for synchronous calls (errors would throw immediately)
                return _makePromise(value);
            }
        };
    }

    // ---- worker thread calls ----
    // These three do the work on a thread of FileHelper's and return a real
    // Promise. Listing the library and reading every wallpaper's project.json
    // used to run on the GUI thread, freezing the shell for as long as it took.
    property var _pending: ({})
    Connections {
        target: fileHelper
        function onAsyncFinished(id, result) {
            if (!root) return;
            const resolve = root._pending[id];
            if (!resolve) return;
            delete root._pending[id];
            resolve(result);
        }
    }
    function _async(id) {
        return new Promise(resolve => { root._pending[id] = resolve; });
    }

    // The settings dialog reads a wallpaper's project.json from several places
    // each time one is selected. With this on, each file is read once.
    property bool cacheProjectFiles: false
    property var _projectFiles: ({})
    function readfile(path) {
        const cache = cacheProjectFiles && String(path).endsWith("/project.json");
        if (cache && _projectFiles.hasOwnProperty(path)) return Promise.resolve(_projectFiles[path]);
        return _async(fileHelper.readFileAsync(path)).then(text => {
            // `root` is null if this object was destroyed while the read was under way
            if (cache && root) root._projectFiles[path] = text;
            return text;
        });
    }

    // Synchronous: true if the file or directory exists
    function exists(path) {
        return fileHelper.exists(path);
    }

    // a folder's size doesn't change while the dialog is open
    property var _dirSizes: ({})
    function get_dir_size(path, depth) {
        if (depth === undefined) depth = 3;
        const key = `${depth}:${path}`;
        if (_dirSizes.hasOwnProperty(key)) return Promise.resolve(_dirSizes[key]);
        return _async(fileHelper.getDirSizeAsync(path, depth)).then(size => {
            if (root) root._dirSizes[key] = size;
            return size;
        });
    }

    function get_folder_list(path, opt) {
        if (opt === undefined) opt = {};
        return _async(fileHelper.getFolderListAsync(path, opt));
    }

    // The per-wallpaper config calls below stay synchronous: one small file,
    // and the caller often needs the answer before it goes on.
    function read_wallpaper_config(id) {
        const config = fileHelper.readWallpaperConfig(id);
        return _makePromise(config);
    }

    function write_wallpaper_config(id, changed) {
        fileHelper.writeWallpaperConfig(id, changed);
        return _makePromise(null);
    }

    function reset_wallpaper_config(id) {
        fileHelper.resetWallpaperConfig(id);
        return _makePromise(null);
    }
}
