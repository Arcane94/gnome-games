// This file is part of GNOME Games. License: GPLv3

[GtkTemplate (ui = "/org/gnome/Games/ui/content-box.ui")]
private class Games.ContentBox : Gtk.Overlay {
	public signal void game_activated (Game game);

	private UiState _ui_state;
	public UiState ui_state {
		set {
			if (value == ui_state)
				return;

			_ui_state = value;

			switch (ui_state) {
			case UiState.COLLECTION:
				content_stack.set_visible_child (collection_icon_view);
				runner = null;

				break;
			case UiState.DISPLAY:
				content_stack.set_visible_child (display_box);

				break;
			}
		}
		get { return _ui_state; }
	}

	public ListModel collection {
		set { collection_icon_view.model = value; }
		get { return collection_icon_view.model; }
	}

	[GtkChild]
	private Gtk.Stack content_stack;
	[GtkChild]
	private CollectionIconView collection_icon_view;
	[GtkChild]
	private Gtk.EventBox display_box;

	private Runner _runner;
	public Runner runner {
		set {
			if (runner != null)
				runner.disconnect (runner_stopped_id);

			_runner = value;
			remove_display ();

			if (runner == null)
				return;

			runner_stopped_id = runner.stopped.connect (on_runner_stopped);

			var display = runner.get_display ();
			set_display (display);
		}
		private get { return _runner; }
	}
	private ulong runner_stopped_id;

	public ContentBox (ListStore collection) {
		collection_icon_view.model = collection;
	}

	[GtkCallback]
	private void on_game_activated (Game game) {
		game_activated (game);
	}

	private void set_display (Gtk.Widget display) {
		remove_display ();
		display_box.add (display);
		display.visible = true;
	}

	private void remove_display () {
		var child = display_box.get_child ();
		if (child != null)
			display_box.remove (display_box.get_child ());
	}

	private void on_runner_stopped () {
		runner = null;
		ui_state = UiState.COLLECTION;
	}
}
