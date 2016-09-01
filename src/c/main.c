/*
   Project: CubeQ3test (watchapp)
   File   : main.c
   Author : Afonso Santos, Portugal

   Last revision: 12h54 August 30 2016
*/

#include <pebble.h>
#include <karambola/Q3.h>
#include <karambola/CamQ3.h>
#include <karambola/Sampler.h>
#include <karambola/CubeQ3.h>

#include "Config.h"

// Obstruction related.
static GSize available_screen ;


// UI related
static Window         *s_window ;
static Layer          *s_window_layer ;
static Layer          *s_world_layer ;
static ActionBarLayer *s_action_bar;


// World related
#define ACCEL_SAMPLER_CAPACITY    8
#define WORLD_UPDATE_INTERVAL_MS  40

GPoint __MeshQ3_vertex_screenPoint[6] ;

static MeshQ3 *s_cube ;  // The main/only world object.

static int        s_world_updateCount       = 0 ;
static AppTimer  *s_world_updateTimer_ptr   = NULL ;

Sampler   *sampler_accelX = NULL ;            // To be allocated at world_initialize( ).
Sampler   *sampler_accelY = NULL ;            // To be allocated at world_initialize( ).
Sampler   *sampler_accelZ = NULL ;            // To be allocated at world_initialize( ).


// Persistence related
#define PKEY_TRANSPARENCY_MODE     2

#define MESH_TRANSPARENCY_DEFAULT  MESH_TRANSPARENCY_SOLID


// Camera related
#define  CAM3D_DISTANCEFROMORIGIN    (Q_make(2.2f))

static CamQ3             s_cam ;
static Q                 s_cam_zoom           = PBL_IF_RECT_ELSE(Q_make(1.25f), Q_make(1.15f)) ;
static MeshTransparency  s_transparencyMode   = MESH_TRANSPARENCY_SOLID ;   // To be loaded/initialized from persistent storage.


void
transparencyMode_change_click_handler
( ClickRecognizerRef recognizer
, void              *context
)
{
  // Cycle trough the transparency modes.
  switch (s_transparencyMode)
  {
    case MESH_TRANSPARENCY_SOLID:
     s_transparencyMode = MESH_TRANSPARENCY_XRAY ;
     break ;

   case MESH_TRANSPARENCY_XRAY:
     s_transparencyMode = MESH_TRANSPARENCY_WIREFRAME ;
     break ;

   case MESH_TRANSPARENCY_WIREFRAME:
   default:
     s_transparencyMode = MESH_TRANSPARENCY_SOLID ;
     break ;
  } ;
}


// Forward declare all click_config_providers( ).
void  normalMode_click_config_provider( void *context ) ;


void
normalMode_click_config_provider
( void *context )
{
  window_single_click_subscribe( BUTTON_ID_SELECT
                               , (ClickHandler) transparencyMode_change_click_handler
                               ) ;
}


// Acellerometer handlers.
void
accel_data_service_handler
( AccelData *data
, uint32_t   num_samples
)
{ }


// Forward declare.
void  world_stop( ) ;
void  world_finalize( ) ;


void
cam_config
( Q3 *viewPoint )
{
  // setup 3D camera
  CamQ3_lookAtOriginUpwards( &s_cam
                           , Q3_scale( CAM3D_DISTANCEFROMORIGIN    // View point.
                                     , viewPoint
                                     )
                           , s_cam_zoom                            // Zoom
                           , CAM_PROJECTION_PERSPECTIVE
                           ) ;
}


static
void
sampler_initialize
( )
{
  sampler_accelX = Sampler_new( ACCEL_SAMPLER_CAPACITY ) ;
  sampler_accelY = Sampler_new( ACCEL_SAMPLER_CAPACITY ) ;
  sampler_accelZ = Sampler_new( ACCEL_SAMPLER_CAPACITY ) ;

  for ( int i = 0  ;  i < ACCEL_SAMPLER_CAPACITY  ;  ++i )
  {
    Sampler_push( sampler_accelX,  -81 ) ;   // STEADY viewPoint attractor.
    Sampler_push( sampler_accelY, -816 ) ;   // STEADY viewPoint attractor.
    Sampler_push( sampler_accelZ, -571 ) ;   // STEADY viewPoint attractor.
  }
}


void
world_initialize
( )
{ // Get previous configuration from persistent storage if it exists, otherwise use the defaults.
  s_transparencyMode = persist_exists(PKEY_TRANSPARENCY_MODE) ? persist_read_int(PKEY_TRANSPARENCY_MODE) : MESH_TRANSPARENCY_DEFAULT ;

  sampler_initialize( ) ;
  s_cube = CubeQ3_new( ) ;
  CubeQ3_config( s_cube, Q_1, NULL ) ;
}


// UPDATE CAMERA & WORLD OBJECTS PROPERTIES

static
void
world_update
( )
{
  ++s_world_updateCount ;

  AccelData ad ;

  if (accel_service_peek( &ad ) < 0)         // Accel service not available.
  {
    Sampler_push( sampler_accelX,  -81 ) ;   // STEADY viewPoint attractor.
    Sampler_push( sampler_accelY, -816 ) ;   // STEADY viewPoint attractor.
    Sampler_push( sampler_accelZ, -571 ) ;   // STEADY viewPoint attractor.
  }
  else
  {
#ifdef QEMU
    if (ad.x == 0  &&  ad.y == 0  &&  ad.z == -1000)   // Under QEMU with SENSORS off this is the default output.
    {
      Sampler_push( sampler_accelX,  -81 ) ;
      Sampler_push( sampler_accelY, -816 ) ;
      Sampler_push( sampler_accelZ, -571 ) ;
    }
    else                                               // If running under QEMU the SENSOR feed must be ON.
    {
      Sampler_push( sampler_accelX, ad.x ) ;
      Sampler_push( sampler_accelY, ad.y ) ;
      Sampler_push( sampler_accelZ, ad.z ) ;
    }
#else
    Sampler_push( sampler_accelX, ad.x ) ;
    Sampler_push( sampler_accelY, ad.y ) ;
    Sampler_push( sampler_accelZ, ad.z ) ;
#endif

    const float kAvg = 0.001f / sampler_accelX->samplesNum ;
    const float avgX = (float)(kAvg * sampler_accelX->samplesAcum ) ;
    const float avgY =-(float)(kAvg * sampler_accelY->samplesAcum ) ;
    const float avgZ =-(float)(kAvg * sampler_accelZ->samplesAcum ) ;

    static Q3 viewPoint ;
    viewPoint.x = Q_make( avgX ) ;
    viewPoint.y = Q_make( avgY ) ;
    viewPoint.z = Q_make( avgZ ) ;
      
    cam_config( &viewPoint ) ;
  }

  // this will queue a defered call to the world_draw( ) method.
  layer_mark_dirty( s_world_layer ) ;
}


#ifdef LOG
static int s_world_draw_count = 0 ;
#endif

void
world_draw
( Layer    *me
, GContext *gCtx
)
{
  // Disable antialiasing if running under QEMU (crashes after a few frames otherwise).
#ifdef QEMU
  graphics_context_set_antialiased( gCtx, false ) ;
#endif

  MeshQ3_draw( gCtx, s_cube, &s_cam, available_screen.w, available_screen.h, s_transparencyMode ) ;
}


static
void
sampler_finalize
( )
{
  free( Sampler_free( sampler_accelX ) ) ; sampler_accelX = NULL ;
  free( Sampler_free( sampler_accelY ) ) ; sampler_accelY = NULL ;
  free( Sampler_free( sampler_accelZ ) ) ; sampler_accelZ = NULL ;
}


void
world_finalize
( )
{
  MeshQ3_free( s_cube ) ;
  sampler_finalize( ) ;

  // Save current configuration into persistent storage on app exit.
  persist_write_int( PKEY_TRANSPARENCY_MODE, s_transparencyMode ) ;
}


void
world_update_timer_handler
( void *data )
{
  world_update( ) ;

  // Call me again.
  s_world_updateTimer_ptr = app_timer_register( WORLD_UPDATE_INTERVAL_MS, world_update_timer_handler, data ) ;
}


void
world_start
( )
{
  // Set initial world mode (and subscribe to related services).
 	accel_data_service_subscribe( 0, accel_data_service_handler ) ;

  // Trigger call to launch animation, will self repeat.
  world_update_timer_handler( NULL ) ;
}


void
world_stop
( )
{
  // Stop animation.
  app_timer_cancel( s_world_updateTimer_ptr ) ;

  // Gravity unaware.
  accel_data_service_unsubscribe( ) ;
}


void
unobstructed_area_change_handler
( AnimationProgress progress
, void             *context
)
{
  available_screen = layer_get_unobstructed_bounds( s_window_layer ).size ;
}


void
window_load
( Window *s_window )
{
  s_window_layer    = window_get_root_layer( s_window ) ;
  available_screen  = layer_get_unobstructed_bounds( s_window_layer ).size ;

  s_action_bar = action_bar_layer_create( ) ;
  action_bar_layer_add_to_window( s_action_bar, s_window ) ;
  action_bar_layer_set_click_config_provider( s_action_bar, normalMode_click_config_provider ) ;

  GRect bounds = layer_get_frame( s_window_layer ) ;
  s_world_layer = layer_create( bounds ) ;
  layer_set_update_proc( s_world_layer, world_draw ) ;
  layer_add_child( s_window_layer, s_world_layer ) ;

  // Obstrution handling.
  UnobstructedAreaHandlers unobstructed_area_handlers = { .change = unobstructed_area_change_handler } ;
  unobstructed_area_service_subscribe( unobstructed_area_handlers, NULL ) ;

  // Position s_cube handles according to current time, launch blinkers, launch animation, start s_cube.
  world_start( ) ;
}


void
window_unload
( Window *s_window )
{
  world_stop( ) ;
  unobstructed_area_service_unsubscribe( ) ;
  layer_destroy( s_world_layer ) ;
}


void
app_init
( void )
{
  world_initialize( ) ;

  s_window = window_create( ) ;
  window_set_background_color( s_window, GColorBlack ) ;
 
  window_set_window_handlers( s_window
                            , (WindowHandlers)
                              { .load   = window_load
                              , .unload = window_unload
                              }
                            ) ;

  window_stack_push( s_window, false ) ;
}


void
app_deinit
( void )
{
  window_stack_remove( s_window, false ) ;
  window_destroy( s_window ) ;
  world_finalize( ) ;
}


int
main
( void )
{
  app_init( ) ;
  app_event_loop( ) ;
  app_deinit( ) ;
}
