#ifndef GAMES_GAMEPAD_MAPPINGS_MANAGER_H
#define GAMES_GAMEPAD_MAPPINGS_MANAGER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GAMES_TYPE_GAMEPAD_MAPPINGS_MANAGER (games_gamepad_mappings_manager_get_type())

G_DECLARE_FINAL_TYPE (GamesGamepadMappingsManager, games_gamepad_mappings_manager, GAMES, GAMEPAD_MAPPINGS_MANAGER, GObject)

GamesGamepadMappingsManager *games_gamepad_mappings_manager_get_instance (void);
gchar *games_gamepad_mappings_manager_get_mapping (GamesGamepadMappingsManager *self,
                                                   const gchar                 *guid);

G_END_DECLS

#endif /* GAMES_GAMEPAD_MAPPINGS_MANAGER_H */

