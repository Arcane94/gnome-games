// This file is part of GNOME Games. License: GPLv3

[GtkTemplate (ui = "/org/gnome/Games/ui/application-window.ui")]
private class Games.ApplicationWindow : Gtk.ApplicationWindow {
	public UiState ui_state { set; get; }

	[GtkChild]
	private ContentBox content_box;
	private Binding cb_ui_binding;

	[GtkChild]
	private HeaderBar header_bar;
	private Binding hb_ui_binding;

	private HashTable<Game, Runner> runners;

	public ApplicationWindow (ListModel collection) {
		content_box.collection = collection;
	}

	construct {
		runners = new HashTable<Game, Runner> (GLib.direct_hash, GLib.direct_equal);

		cb_ui_binding = content_box.bind_property ("ui-state",
		                                          this, "ui-state", BindingFlags.BIDIRECTIONAL);
		hb_ui_binding = header_bar.bind_property ("ui-state",
		                                          this, "ui-state", BindingFlags.BIDIRECTIONAL);
	}

	[GtkCallback]
	private void on_game_activated (Game game) {
		Runner runner = null;
		try {
			runner = get_runner_for_game (game);
		}
		catch (Error e) {
			warning ("%s\n", e.message);
			content_box.display_error (e.message);

			return;
		}

		content_box.runner = runner;
		ui_state = UiState.DISPLAY;

		var resume = false;

		if (runner.can_resume) {
			var dialog = new ResumeDialog ();
			dialog.set_transient_for (this);
			var response = dialog.run ();
			dialog.destroy ();

			switch (response) {
			case Gtk.ResponseType.CANCEL:
				content_box.runner = null;
				ui_state = UiState.COLLECTION;

				return;
			case Gtk.ResponseType.REJECT:
				resume = false;

				break;
			case Gtk.ResponseType.ACCEPT:
			default:
				resume = true;

				break;
			}
		}

		try {
			if (resume)
				runner.resume ();
			else
				runner.start ();
		}
		catch (RunError e) {
			warning (@"$(e.message)\n");

			return;
		}
	}

	private Runner get_runner_for_game (Game game) throws RunError {
		if (runners.contains (game))
			return runners[game];

		var runner = game.get_runner ();
		runners[game] = runner;

		runner.stopped.connect (remove_runner);

		return runner;
	}

	private void remove_runner (Runner runner) {
		foreach (var game in runners.get_keys ()) {
			if (runners[game] == runner)
				runners.remove (game);
		}
	}
}