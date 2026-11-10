"""GTK3 drag-and-drop frontend for the source installer."""
from pathlib import Path
import subprocess
import threading
from urllib.parse import unquote, urlparse
import gi
gi.require_version('Gtk','3.0')
from gi.repository import Gtk, Gdk, GLib
from install import Installer, InstallError, Cancelled

class Window(Gtk.Window):
    def __init__(self,args):
        super().__init__(title='Install Mario Party 2 Recompiled')
        self.args=args;self.installer=None;self.running=False;self.close_when_done=False
        self.set_default_size(640,610);self.set_border_width(24)
        self.connect('delete-event',self.close)
        layout=Gtk.Box(orientation=Gtk.Orientation.VERTICAL,spacing=14);self.add(layout)
        title=Gtk.Label(label='Mario Party 2 Recompiled',xalign=0)
        title.set_markup('<span size="xx-large" weight="bold">Mario Party 2 Recompiled</span>');layout.pack_start(title,False,False,0)
        subtitle=Gtk.Label(label='Bring your USA ROM. Drop it below and hit Go.',xalign=0)
        layout.pack_start(subtitle,False,False,0)
        self.drop=Gtk.EventBox();self.drop.set_size_request(-1,110)
        self.drop.add(Gtk.Label(label='Drop your .z64, .v64, or .n64 ROM here\nor click to choose a file'))
        self.drop.drag_dest_set(Gtk.DestDefaults.ALL,[],Gdk.DragAction.COPY)
        self.drop.drag_dest_add_uri_targets();self.drop.connect('drag-data-received',self.dropped)
        self.drop.connect('button-press-event',lambda *_:self.choose_rom());layout.pack_start(self.drop,False,False,0)
        self.rom=Gtk.Entry();self.rom.set_placeholder_text('ROM file');self.rom.set_text(str(args.rom or ''))
        layout.pack_start(self.rom,False,False,0)
        row=Gtk.Box(spacing=8);layout.pack_start(row,False,False,0)
        self.prefix=Gtk.Entry();self.prefix.set_text(str(args.prefix));self.prefix.set_hexpand(True)
        row.pack_start(Gtk.Label(label='Install to'),False,False,0);row.pack_start(self.prefix,True,True,0)
        folder=Gtk.Button(label='Browse');folder.connect('clicked',self.choose_folder);row.pack_start(folder,False,False,0)
        info=Gtk.Label(label='The game is built locally from your ROM. Internet access and\nbuild tools are required; the first install can take several minutes.',xalign=0)
        layout.pack_start(info,False,False,0)
        advanced=Gtk.Expander(label='Build options');layout.pack_start(advanced,False,False,0)
        options=Gtk.Box(orientation=Gtk.Orientation.VERTICAL,spacing=8);advanced.add(options)
        self.cache=Gtk.Entry();self.cache.set_placeholder_text('Build directory (automatic cache by default)');self.cache.set_text(str(args.build_dir or ''));options.pack_start(self.cache,False,False,0)
        jobs=Gtk.Box(spacing=8);options.pack_start(jobs,False,False,0)
        jobs.pack_start(Gtk.Label(label='Parallel build jobs'),False,False,0)
        self.jobs=Gtk.SpinButton.new_with_range(1,256,1);self.jobs.set_value(args.jobs);jobs.pack_start(self.jobs,False,False,0)
        self.desktop=Gtk.CheckButton(label='Add to the application menu');self.desktop.set_active(not args.no_desktop);options.pack_start(self.desktop,False,False,0)
        self.launch=Gtk.CheckButton(label='Launch when finished');self.launch.set_active(True);layout.pack_start(self.launch,False,False,0)
        self.status=Gtk.Label(label='Ready. Select your Mario Party 2 USA ROM.',xalign=0);self.status.set_line_wrap(True);self.status.set_selectable(True);layout.pack_start(self.status,False,False,0)
        self.progress=Gtk.ProgressBar();layout.pack_start(self.progress,False,False,0)
        details=Gtk.Expander(label='Installation log');layout.pack_start(details,True,True,0)
        scroll=Gtk.ScrolledWindow();scroll.set_min_content_height(140);details.add(scroll)
        self.log=Gtk.TextView();self.log.set_editable(False);self.log.set_monospace(True);scroll.add(self.log)
        actions=Gtk.Box(spacing=8);layout.pack_start(actions,False,False,0)
        self.cancel=Gtk.Button(label='Cancel');self.cancel.set_sensitive(False);self.cancel.connect('clicked',lambda *_:self.cancel_install());actions.pack_start(self.cancel,False,False,0)
        self.go=Gtk.Button(label='Go');self.go.get_style_context().add_class('suggested-action');self.go.connect('clicked',self.start);actions.pack_end(self.go,False,False,0)
        self.controls=[self.drop,self.rom,self.prefix,folder,self.cache,self.jobs,self.desktop,self.launch]
        self.show_all()

    def choose_rom(self):
        if self.running:return
        dialog=Gtk.FileChooserDialog(title='Choose your Mario Party 2 USA ROM',transient_for=self,action=Gtk.FileChooserAction.OPEN)
        dialog.add_buttons('Cancel',Gtk.ResponseType.CANCEL,'Select',Gtk.ResponseType.OK)
        if dialog.run()==Gtk.ResponseType.OK:self.rom.set_text(dialog.get_filename())
        dialog.destroy()

    def choose_folder(self,*_):
        dialog=Gtk.FileChooserDialog(title='Choose install directory',transient_for=self,action=Gtk.FileChooserAction.SELECT_FOLDER)
        dialog.add_buttons('Cancel',Gtk.ResponseType.CANCEL,'Select',Gtk.ResponseType.OK)
        if dialog.run()==Gtk.ResponseType.OK:self.prefix.set_text(dialog.get_filename())
        dialog.destroy()

    def dropped(self,widget,context,x,y,selection,info,time):
        uris=selection.get_uris() or []
        if len(uris)==1 and not self.running:
            uri=urlparse(uris[0])
            if uri.scheme=='file' and uri.netloc in ('','localhost'):
                self.rom.set_text(unquote(uri.path));context.finish(True,False,time);return
        self.status.set_text('Drop a single local ROM file.');context.finish(False,False,time)

    def append(self,line):
        buffer=self.log.get_buffer();buffer.insert(buffer.get_end_iter(),line+'\n')
        # Keep the GUI responsive during long compiler output.
        if buffer.get_line_count()>2000:buffer.delete(buffer.get_start_iter(),buffer.get_iter_at_line(500))
        self.log.scroll_to_iter(buffer.get_end_iter(),0,False,0,1)
        return False

    def cancel_install(self):
        if self.installer:
            self.status.set_text('Cancelling…');self.cancel.set_sensitive(False);self.installer.cancel()

    def close(self,*_):
        if self.running:
            self.close_when_done=True;self.cancel_install();return True
        Gtk.main_quit();return False

    def start(self,*_):
        if self.running:return
        rom=self.rom.get_text().strip();prefix=self.prefix.get_text().strip();cache=self.cache.get_text().strip()
        if not rom or not prefix:self.status.set_text('Select a ROM and install directory.');return
        jobs=self.jobs.get_value_as_int();desktop=self.desktop.get_active();launch=self.launch.get_active()
        self.running=True;self.go.set_sensitive(False);self.cancel.set_sensitive(True)
        for control in self.controls:control.set_sensitive(False)
        self.installer=Installer(status=lambda s:GLib.idle_add(self.status.set_text,s),log=lambda s:GLib.idle_add(self.append,s))
        def pulse():
            if self.running:self.progress.pulse();return True
            return False
        GLib.timeout_add(120,pulse)
        def work():
            launcher=None;error=None
            try:launcher=self.installer.install(rom,prefix,cache or None,jobs,desktop)
            except (InstallError,OSError,ValueError) as exc:error=str(exc)
            except Exception as exc:error='Installation failed: '+str(exc)
            GLib.idle_add(self.finished,launcher,error,launch)
        threading.Thread(target=work,daemon=True).start()

    def finished(self,launcher,error,launch):
        self.running=False;self.cancel.set_sensitive(False);self.go.set_sensitive(True)
        for control in self.controls:control.set_sensitive(True)
        if error:self.status.set_text(error);self.progress.set_fraction(0)
        else:
            self.status.set_text('Installed. Launcher: '+str(launcher));self.progress.set_fraction(1)
            if launch and not self.close_when_done:
                try:subprocess.Popen([str(launcher)],start_new_session=True)
                except OSError as exc:self.status.set_text('Installed, but could not launch: '+str(exc))
        if self.close_when_done:Gtk.main_quit()
        return False

def run_gui(args):
    if not Gtk.init_check()[0]:raise InstallError('No graphical display is available. Use --rom for CLI installation.')
    window=Window(args);Gtk.main();return 0
