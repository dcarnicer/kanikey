// The single application state instance. Constructed before setup() because
// the field constructors (String, vectors, WiFiServer) used to be globals.
#include "app/AppState.h"

namespace kk::app {
namespace detail {

AppState gAppState;

}  // namespace detail
}  // namespace kk::app
