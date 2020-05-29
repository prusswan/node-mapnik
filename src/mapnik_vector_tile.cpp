#include "utils.hpp"
#include "mapnik_map.hpp"
#include "mapnik_image.hpp"
#if defined(GRID_RENDERER)
#include "mapnik_grid.hpp"
#endif
#include "mapnik_feature.hpp"
#include "mapnik_cairo_surface.hpp"
#ifdef SVG_RENDERER
#include <mapnik/svg/output/svg_renderer.hpp>
#endif

#include "mapnik_vector_tile.hpp"
#include "vector_tile_compression.hpp"
#include "vector_tile_composite.hpp"
#include "vector_tile_processor.hpp"
#include "vector_tile_projection.hpp"
#include "vector_tile_datasource_pbf.hpp"
#include "vector_tile_geometry_decoder.hpp"
#include "vector_tile_load_tile.hpp"
#include "object_to_container.hpp"

// mapnik
#include <mapnik/agg_renderer.hpp>      // for agg_renderer
#include <mapnik/datasource_cache.hpp>
#include <mapnik/geometry/box2d.hpp>
#include <mapnik/feature.hpp>
#include <mapnik/featureset.hpp>
#include <mapnik/feature_kv_iterator.hpp>
#include <mapnik/geometry/is_simple.hpp>
#include <mapnik/geometry/is_valid.hpp>
#include <mapnik/geometry/reprojection.hpp>
#include <mapnik/util/feature_to_geojson.hpp>
#include <mapnik/hit_test_filter.hpp>
#include <mapnik/image_any.hpp>
#include <mapnik/layer.hpp>
#include <mapnik/map.hpp>
#include <mapnik/memory_datasource.hpp>
#include <mapnik/projection.hpp>
#include <mapnik/request.hpp>
#include <mapnik/scale_denominator.hpp>
#include <mapnik/version.hpp>
#if defined(GRID_RENDERER)
#include <mapnik/grid/grid.hpp>         // for hit_grid, grid
#include <mapnik/grid/grid_renderer.hpp>  // for grid_renderer
#endif
#ifdef HAVE_CAIRO
#include <mapnik/cairo/cairo_renderer.hpp>
#include <cairo.h>
#ifdef CAIRO_HAS_SVG_SURFACE
#include <cairo-svg.h>
#endif // CAIRO_HAS_SVG_SURFACE
#endif

// std
#include <set>                          // for set, etc
#include <sstream>                      // for operator<<, basic_ostream, etc
#include <string>                       // for string, char_traits, etc
#include <exception>                    // for exception
#include <vector>                       // for vector

// protozero
#include <protozero/pbf_reader.hpp>

Napi::FunctionReference VectorTile::constructor;

//

Napi::Object  VectorTile::Initialize(Napi::Env env, Napi::Object exports)
{
    Napi::HandleScope scope(env);
    Napi::Function func = DefineClass(env, "VectorTile", {
            InstanceAccessor<&VectorTile::get_tile_x, &VectorTile::set_tile_x>("x"),
            InstanceAccessor<&VectorTile::get_tile_y, &VectorTile::set_tile_y>("y"),
            InstanceAccessor<&VectorTile::get_tile_z, &VectorTile::set_tile_z>("z"),
            InstanceAccessor<&VectorTile::get_tile_size, &VectorTile::set_tile_size>("tileSize"),
            InstanceAccessor<&VectorTile::get_buffer_size, &VectorTile::set_buffer_size>("bufferSize"),
            InstanceMethod<&VectorTile::render>("render"),
            InstanceMethod<&VectorTile::setData>("setData"),
            InstanceMethod<&VectorTile::setDataSync>("setDataSync"),
            InstanceMethod<&VectorTile::getData>("getData"),
            InstanceMethod<&VectorTile::getDataSync>("getDataSync"),
            InstanceMethod<&VectorTile::addData>("addData"),
            InstanceMethod<&VectorTile::addDataSync>("addDataSync"),
            InstanceMethod<&VectorTile::composite>("composite"),
            InstanceMethod<&VectorTile::compositeSync>("compositeSync"),
            InstanceMethod<&VectorTile::query>("query"),
            InstanceMethod<&VectorTile::queryMany>("queryMany"),
            InstanceMethod<&VectorTile::extent>("extent"),
            InstanceMethod<&VectorTile::bufferedExtent>("bufferedExtent"),
            InstanceMethod<&VectorTile::names>("names"),
            InstanceMethod<&VectorTile::layer>("layer"),
            InstanceMethod<&VectorTile::emptyLayers>("emptyLayerss"),
            InstanceMethod<&VectorTile::paintedLayers>("paintedLayers"),
            InstanceMethod<&VectorTile::toJSON>("toJSON"),
            InstanceMethod<&VectorTile::toGeoJSON>("toGeoJSON"),
            InstanceMethod<&VectorTile::toGeoJSONSync>("toGeoJSONSync"),
            InstanceMethod<&VectorTile::addGeoJSON>("addGeoJSON"),
            InstanceMethod<&VectorTile::addImage>("addImage"),
            InstanceMethod<&VectorTile::addImageSync>("addImageSync"),
            InstanceMethod<&VectorTile::addImageBuffer>("addImageBuffer"),
            InstanceMethod<&VectorTile::addImageBufferSync>("addImageBufferSync"),
#if BOOST_VERSION >= 105600
            InstanceMethod<&VectorTile::reportGeometrySimplicity>("reportGeometrySimplicity"),
            InstanceMethod<&VectorTile::reportGeometrySimplicitySync>("reportGeometrySimplicitySync"),
            InstanceMethod<&VectorTile::reportGeometryValidity>("reportGeometryValidity"),
            InstanceMethod<&VectorTile::reportGeometryValiditySync>("reportGeometryValiditySync"),
#endif
            InstanceMethod<&VectorTile::painted>("painted"),
            InstanceMethod<&VectorTile::clear>("clear"),
            InstanceMethod<&VectorTile::clearSync>("clearSync"),
            InstanceMethod<&VectorTile::empty>("empty"),
            InstanceMethod<&VectorTile::info>("info")
        });
    constructor = Napi::Persistent(func);
    constructor.SuppressDestruct();
    exports.Set("VectorTile", func);
    return exports;
}

/**
 * **`mapnik.VectorTile`**

 * A tile generator built according to the [Mapbox Vector Tile](https://github.com/mapbox/vector-tile-spec)
 * specification for compressed and simplified tiled vector data.
 * Learn more about vector tiles [here](https://www.mapbox.com/developers/vector-tiles/).
 *
 * @class VectorTile
 * @param {number} z - an integer zoom level
 * @param {number} x - an integer x coordinate
 * @param {number} y - an integer y coordinate
 * @property {number} x - horizontal axis position
 * @property {number} y - vertical axis position
 * @property {number} z - the zoom level
 * @property {number} tileSize - the size of the tile
 * @property {number} bufferSize - the size of the tile's buffer
 * @example
 * var vt = new mapnik.VectorTile(9,112,195);
 * console.log(vt.z, vt.x, vt.y); // 9, 112, 195
 * console.log(vt.tileSize, vt.bufferSize); // 4096, 128
 */

VectorTile::VectorTile(Napi::CallbackInfo const& info)
    : Napi::ObjectWrap<VectorTile>(info)
{
    Napi::Env env = info.Env();
    if (info.Length() == 1 && info[0].IsExternal())
    {
        auto ext = info[0].As<Napi::External<mapnik::vector_tile_impl::merc_tile_ptr>>();
        if (ext) tile_ = *ext.Data();
        return;
    }

    if (info.Length() < 3)
    {
        Napi::Error::New(env, "please provide a z, x, y").ThrowAsJavaScriptException();
        return;
    }

    if (!info[0].IsNumber() ||
        !info[1].IsNumber() ||
        !info[2].IsNumber())
    {
        Napi::TypeError::New(env, "required parameters (z, x, and y) must be a integers").ThrowAsJavaScriptException();
        return;
    }

    std::int64_t z = info[0].As<Napi::Number>().Int64Value();
    std::int64_t x = info[1].As<Napi::Number>().Int64Value();
    std::int64_t y = info[2].As<Napi::Number>().Int64Value();
    if (z < 0 || x < 0 || y < 0)
    {
        Napi::TypeError::New(env, "required parameters (z, x, and y) must be greater then or equal to zero").ThrowAsJavaScriptException();
        return;
    }
    std::int64_t max_at_zoom = pow(2,z);
    if (x >= max_at_zoom)
    {
        Napi::TypeError::New(env, "required parameter x is out of range of possible values based on z value").ThrowAsJavaScriptException();
        return;
    }
    if (y >= max_at_zoom)
    {
        Napi::TypeError::New(env, "required parameter y is out of range of possible values based on z value").ThrowAsJavaScriptException();
        return;
    }

    std::uint32_t tile_size = 4096;
    std::int32_t buffer_size = 128;
    Napi::Object options = Napi::Object::New(env);
    if (info.Length() > 3)
    {
        if (!info[3].IsObject())
        {
            Napi::TypeError::New(env, "optional fourth argument must be an options object").ThrowAsJavaScriptException();
            return;
        }
        options = info[3].As<Napi::Object>();
        if (options.Has("tile_size"))
        {
            Napi::Value opt = options.Get("tile_size");
            if (!opt.IsNumber())
            {
                Napi::TypeError::New(env, "optional arg 'tile_size' must be a number").ThrowAsJavaScriptException();
                return;
            }
            int tile_size_tmp = opt.As<Napi::Number>().Int32Value();
            if (tile_size_tmp <= 0)
            {
                Napi::TypeError::New(env, "optional arg 'tile_size' must be greater then zero").ThrowAsJavaScriptException();
                return;
            }
            tile_size = tile_size_tmp;
        }
        if (options.Has("buffer_size"))
        {
            Napi::Value opt = options.Get("buffer_size");
            if (!opt.IsNumber())
            {
                Napi::TypeError::New(env, "optional arg 'buffer_size' must be a number").ThrowAsJavaScriptException();
                return;
            }
            buffer_size = opt.As<Napi::Number>().Int32Value();
        }
    }
    if (static_cast<double>(tile_size) + (2 * buffer_size) <= 0)
    {
        Napi::Error::New(env, "too large of a negative buffer for tilesize").ThrowAsJavaScriptException();
        return;
    }
    tile_ = std::make_shared<mapnik::vector_tile_impl::merc_tile>(x, y, z, tile_size, buffer_size);
}
/*
void _composite(VectorTile* target_vt,
                std::vector<VectorTile*> & vtiles,
                double scale_factor,
                unsigned offset_x,
                unsigned offset_y,
                double area_threshold,
                bool strictly_simple,
                bool multi_polygon_union,
                mapnik::vector_tile_impl::polygon_fill_type fill_type,
                double scale_denominator,
                bool reencode,
                boost::optional<mapnik::box2d<double>> const& max_extent,
                double simplify_distance,
                bool process_all_rings,
                std::string const& image_format,
                mapnik::scaling_method_e scaling_method,
                std::launch threading_mode)
{
    // create map
    mapnik::Map map(target_vt->tile_size(),target_vt->tile_size(),"+init=epsg:3857");
    if (max_extent)
    {
        map.set_maximum_extent(*max_extent);
    }
    else
    {
        map.set_maximum_extent(target_vt->get_tile()->get_buffered_extent());
    }

    std::vector<mapnik::vector_tile_impl::merc_tile_ptr> merc_vtiles;
    for (VectorTile* vt : vtiles)
    {
        merc_vtiles.push_back(vt->get_tile());
    }

    mapnik::vector_tile_impl::processor ren(map);
    ren.set_fill_type(fill_type);
    ren.set_simplify_distance(simplify_distance);
    ren.set_process_all_rings(process_all_rings);
    ren.set_multi_polygon_union(multi_polygon_union);
    ren.set_strictly_simple(strictly_simple);
    ren.set_area_threshold(area_threshold);
    ren.set_scale_factor(scale_factor);
    ren.set_scaling_method(scaling_method);
    ren.set_image_format(image_format);
    ren.set_threading_mode(threading_mode);

    mapnik::vector_tile_impl::composite(*target_vt->get_tile(),
                                        merc_vtiles,
                                        map,
                                        ren,
                                        scale_denominator,
                                        offset_x,
                                        offset_y,
                                        reencode);
}
*/

/**
 * Synchronous version of {@link #VectorTile.composite}
 *
 * @name compositeSync
 * @memberof VectorTile
 * @instance
 * @instance
 * @param {Array<mapnik.VectorTile>} array - an array of vector tile objects
 * @param {object} [options]
 * @example
 * var vt1 = new mapnik.VectorTile(0,0,0);
 * var vt2 = new mapnik.VectorTile(0,0,0);
 * var options = { ... };
 * vt1.compositeSync([vt2], options);
 *
 */
Napi::Value VectorTile::compositeSync(Napi::CallbackInfo const& info)
{
    Napi::Env env = info.Env();
    Napi::EscapableHandleScope scope(env);
    return env.Undefined();
    /*
    if (info.Length() < 1 || !info[0].IsArray())
    {
        Napi::TypeError::New(env, "must provide an array of VectorTile objects and an optional options object").ThrowAsJavaScriptException();

        return scope.Escape(env.Undefined());
    }
    Napi::Array vtiles = info[0].As<Napi::Array>();
    unsigned num_tiles = vtiles->Length();
    if (num_tiles < 1)
    {
        Napi::TypeError::New(env, "must provide an array with at least one VectorTile object and an optional options object").ThrowAsJavaScriptException();

        return scope.Escape(env.Undefined());
    }

    // options needed for re-rendering tiles
    // unclear yet to what extent these need to be user
    // driven, but we expose here to avoid hardcoding
    double scale_factor = 1.0;
    unsigned offset_x = 0;
    unsigned offset_y = 0;
    double area_threshold = 0.1;
    bool strictly_simple = true;
    bool multi_polygon_union = false;
    mapnik::vector_tile_impl::polygon_fill_type fill_type = mapnik::vector_tile_impl::positive_fill;
    double scale_denominator = 0.0;
    bool reencode = false;
    boost::optional<mapnik::box2d<double>> max_extent;
    double simplify_distance = 0.0;
    bool process_all_rings = false;
    std::string image_format = "webp";
    mapnik::scaling_method_e scaling_method = mapnik::SCALING_BILINEAR;
    std::launch threading_mode = std::launch::deferred;

    if (info.Length() > 1)
    {
        // options object
        if (!info[1].IsObject())
        {
            Napi::TypeError::New(env, "optional second argument must be an options object").ThrowAsJavaScriptException();

            return scope.Escape(env.Undefined());
        }
        Napi::Object options = info[1].ToObject(Napi::GetCurrentContext());
        if ((options).Has(Napi::String::New(env, "area_threshold")).FromMaybe(false))
        {
            Napi::Value area_thres = (options).Get(Napi::String::New(env, "area_threshold"));
            if (!area_thres.IsNumber())
            {
                Napi::TypeError::New(env, "option 'area_threshold' must be an floating point number").ThrowAsJavaScriptException();

                return scope.Escape(env.Undefined());
            }
            area_threshold = area_thres.As<Napi::Number>().DoubleValue();
            if (area_threshold < 0.0)
            {
                Napi::TypeError::New(env, "option 'area_threshold' can not be negative").ThrowAsJavaScriptException();

                return scope.Escape(env.Undefined());
            }
        }
        if ((options).Has(Napi::String::New(env, "simplify_distance")).FromMaybe(false))
        {
            Napi::Value param_val = (options).Get(Napi::String::New(env, "simplify_distance"));
            if (!param_val.IsNumber())
            {
                Napi::TypeError::New(env, "option 'simplify_distance' must be an floating point number").ThrowAsJavaScriptException();

                return scope.Escape(env.Undefined());
            }
            simplify_distance = param_val.As<Napi::Number>().DoubleValue();
            if (simplify_distance < 0.0)
            {
                Napi::TypeError::New(env, "option 'simplify_distance' can not be negative").ThrowAsJavaScriptException();

                return scope.Escape(env.Undefined());
            }
        }
        if ((options).Has(Napi::String::New(env, "strictly_simple")).FromMaybe(false))
        {
            Napi::Value strict_simp = (options).Get(Napi::String::New(env, "strictly_simple"));
            if (!strict_simp->IsBoolean())
            {
                Napi::TypeError::New(env, "option 'strictly_simple' must be a boolean").ThrowAsJavaScriptException();

                return scope.Escape(env.Undefined());
            }
            strictly_simple = strict_simp.As<Napi::Boolean>().Value();
        }
        if ((options).Has(Napi::String::New(env, "multi_polygon_union")).FromMaybe(false))
        {
            Napi::Value mpu = (options).Get(Napi::String::New(env, "multi_polygon_union"));
            if (!mpu->IsBoolean())
            {
                Napi::TypeError::New(env, "option 'multi_polygon_union' must be a boolean").ThrowAsJavaScriptException();

                return scope.Escape(env.Undefined());
            }
            multi_polygon_union = mpu.As<Napi::Boolean>().Value();
        }
        if ((options).Has(Napi::String::New(env, "fill_type")).FromMaybe(false))
        {
            Napi::Value ft = (options).Get(Napi::String::New(env, "fill_type"));
            if (!ft.IsNumber())
            {
                Napi::TypeError::New(env, "optional arg 'fill_type' must be a number").ThrowAsJavaScriptException();

                return scope.Escape(env.Undefined());
            }
            fill_type = static_cast<mapnik::vector_tile_impl::polygon_fill_type>(ft.As<Napi::Number>().Int32Value());
            if (fill_type >= mapnik::vector_tile_impl::polygon_fill_type_max)
            {
                Napi::TypeError::New(env, "optional arg 'fill_type' out of possible range").ThrowAsJavaScriptException();

                return scope.Escape(env.Undefined());
            }
        }
        if ((options).Has(Napi::String::New(env, "threading_mode")).FromMaybe(false))
        {
            Napi::Value param_val = (options).Get(Napi::String::New(env, "threading_mode"));
            if (!param_val.IsNumber())
            {
                Napi::TypeError::New(env, "option 'threading_mode' must be an unsigned integer").ThrowAsJavaScriptException();

                return scope.Escape(env.Undefined());
            }
            threading_mode = static_cast<std::launch>(param_val.As<Napi::Number>().Int32Value());
            if (threading_mode != std::launch::async &&
                threading_mode != std::launch::deferred &&
                threading_mode != (std::launch::async | std::launch::deferred))
            {
                Napi::TypeError::New(env, "optional arg 'threading_mode' is invalid").ThrowAsJavaScriptException();

                return scope.Escape(env.Undefined());
            }
        }
        if ((options).Has(Napi::String::New(env, "scale")).FromMaybe(false))
        {
            Napi::Value bind_opt = (options).Get(Napi::String::New(env, "scale"));
            if (!bind_opt.IsNumber())
            {
                Napi::TypeError::New(env, "optional arg 'scale' must be a number").ThrowAsJavaScriptException();

                return scope.Escape(env.Undefined());
            }
            scale_factor = bind_opt.As<Napi::Number>().DoubleValue();
            if (scale_factor <= 0.0)
            {
                Napi::TypeError::New(env, "optional arg 'scale' must be greater then zero").ThrowAsJavaScriptException();

                return scope.Escape(env.Undefined());
            }
        }
        if ((options).Has(Napi::String::New(env, "scale_denominator")).FromMaybe(false))
        {
            Napi::Value bind_opt = (options).Get(Napi::String::New(env, "scale_denominator"));
            if (!bind_opt.IsNumber())
            {
                Napi::TypeError::New(env, "optional arg 'scale_denominator' must be a number").ThrowAsJavaScriptException();

                return scope.Escape(env.Undefined());
            }
            scale_denominator = bind_opt.As<Napi::Number>().DoubleValue();
            if (scale_denominator < 0.0)
            {
                Napi::TypeError::New(env, "optional arg 'scale_denominator' must be non negative number").ThrowAsJavaScriptException();

                return scope.Escape(env.Undefined());
            }
        }
        if ((options).Has(Napi::String::New(env, "offset_x")).FromMaybe(false))
        {
            Napi::Value bind_opt = (options).Get(Napi::String::New(env, "offset_x"));
            if (!bind_opt.IsNumber())
            {
                Napi::TypeError::New(env, "optional arg 'offset_x' must be a number").ThrowAsJavaScriptException();

                return scope.Escape(env.Undefined());
            }
            offset_x = bind_opt.As<Napi::Number>().Int32Value();
        }
        if ((options).Has(Napi::String::New(env, "offset_y")).FromMaybe(false))
        {
            Napi::Value bind_opt = (options).Get(Napi::String::New(env, "offset_y"));
            if (!bind_opt.IsNumber())
            {
                Napi::TypeError::New(env, "optional arg 'offset_y' must be a number").ThrowAsJavaScriptException();

                return scope.Escape(env.Undefined());
            }
            offset_y = bind_opt.As<Napi::Number>().Int32Value();
        }
        if ((options).Has(Napi::String::New(env, "reencode")).FromMaybe(false))
        {
            Napi::Value reencode_opt = (options).Get(Napi::String::New(env, "reencode"));
            if (!reencode_opt->IsBoolean())
            {
                Napi::TypeError::New(env, "reencode value must be a boolean").ThrowAsJavaScriptException();

                return scope.Escape(env.Undefined());
            }
            reencode = reencode_opt.As<Napi::Boolean>().Value();
        }
        if ((options).Has(Napi::String::New(env, "max_extent")).FromMaybe(false))
        {
            Napi::Value max_extent_opt = (options).Get(Napi::String::New(env, "max_extent"));
            if (!max_extent_opt->IsArray())
            {
                Napi::TypeError::New(env, "max_extent value must be an array of [minx,miny,maxx,maxy]").ThrowAsJavaScriptException();

                return scope.Escape(env.Undefined());
            }
            Napi::Array bbox = max_extent_opt.As<Napi::Array>();
            auto len = bbox->Length();
            if (!(len == 4))
            {
                Napi::TypeError::New(env, "max_extent value must be an array of [minx,miny,maxx,maxy]").ThrowAsJavaScriptException();

                return scope.Escape(env.Undefined());
            }
            Napi::Value minx = (bbox).Get(0);
            Napi::Value miny = (bbox).Get(1);
            Napi::Value maxx = (bbox).Get(2);
            Napi::Value maxy = (bbox).Get(3);
            if (!minx.IsNumber() || !miny.IsNumber() || !maxx.IsNumber() || !maxy.IsNumber())
            {
                Napi::Error::New(env, "max_extent [minx,miny,maxx,maxy] must be numbers").ThrowAsJavaScriptException();

                return scope.Escape(env.Undefined());
            }
            max_extent = mapnik::box2d<double>(minx.As<Napi::Number>().DoubleValue(),miny.As<Napi::Number>().DoubleValue(),
                                               maxx.As<Napi::Number>().DoubleValue(),maxy.As<Napi::Number>().DoubleValue());
        }
        if ((options).Has(Napi::String::New(env, "process_all_rings")).FromMaybe(false))
        {
            Napi::Value param_val = (options).Get(Napi::String::New(env, "process_all_rings"));
            if (!param_val->IsBoolean())
            {
                Napi::TypeError::New(env, "option 'process_all_rings' must be a boolean").ThrowAsJavaScriptException();

                return scope.Escape(env.Undefined());
            }
            process_all_rings = param_val.As<Napi::Boolean>().Value();
        }

        if ((options).Has(Napi::String::New(env, "image_scaling")).FromMaybe(false))
        {
            Napi::Value param_val = (options).Get(Napi::String::New(env, "image_scaling"));
            if (!param_val.IsString())
            {
                Napi::TypeError::New(env, "option 'image_scaling' must be a string").ThrowAsJavaScriptException();

                return scope.Escape(env.Undefined());
            }
            std::string image_scaling = TOSTR(param_val);
            boost::optional<mapnik::scaling_method_e> method = mapnik::scaling_method_from_string(image_scaling);
            if (!method)
            {
                Napi::TypeError::New(env, "option 'image_scaling' must be a string and a valid scaling method (e.g 'bilinear')").ThrowAsJavaScriptException();

                return scope.Escape(env.Undefined());
            }
            scaling_method = *method;
        }

        if ((options).Has(Napi::String::New(env, "image_format")).FromMaybe(false))
        {
            Napi::Value param_val = (options).Get(Napi::String::New(env, "image_format"));
            if (!param_val.IsString())
            {
                Napi::TypeError::New(env, "option 'image_format' must be a string").ThrowAsJavaScriptException();

                return scope.Escape(env.Undefined());
            }
            image_format = TOSTR(param_val);
        }
    }
    VectorTile* target_vt = info.Holder().Unwrap<VectorTile>();
    std::vector<VectorTile*> vtiles_vec;
    vtiles_vec.reserve(num_tiles);
    for (unsigned j=0;j < num_tiles;++j)
    {
        Napi::Value val = (vtiles).Get(j);
        if (!val.IsObject())
        {
            Napi::TypeError::New(env, "must provide an array of VectorTile objects").ThrowAsJavaScriptException();

            return scope.Escape(env.Undefined());
        }
        Napi::Object tile_obj = val->ToObject(Napi::GetCurrentContext());
        if (tile_obj->IsNull() || tile_obj->IsUndefined() || !Napi::New(env, VectorTile::constructor)->HasInstance(tile_obj))
        {
            Napi::TypeError::New(env, "must provide an array of VectorTile objects").ThrowAsJavaScriptException();

            return scope.Escape(env.Undefined());
        }
        vtiles_vec.push_back(tile_obj).Unwrap<VectorTile>();
    }
    try
    {
        _composite(target_vt,
                   vtiles_vec,
                   scale_factor,
                   offset_x,
                   offset_y,
                   area_threshold,
                   strictly_simple,
                   multi_polygon_union,
                   fill_type,
                   scale_denominator,
                   reencode,
                   max_extent,
                   simplify_distance,
                   process_all_rings,
                   image_format,
                   scaling_method,
                   threading_mode);
    }
    catch (std::exception const& ex)
    {
        Napi::TypeError::New(env, ex.what()).ThrowAsJavaScriptException();

        return scope.Escape(env.Undefined());
    }

    return scope.Escape(env.Undefined());
    */
}

/*
typedef struct
{
    uv_work_t request;
    VectorTile* d;
    double scale_factor;
    unsigned offset_x;
    unsigned offset_y;
    double area_threshold;
    double scale_denominator;
    std::vector<VectorTile*> vtiles;
    bool error;
    bool strictly_simple;
    bool multi_polygon_union;
    mapnik::vector_tile_impl::polygon_fill_type fill_type;
    bool reencode;
    boost::optional<mapnik::box2d<double>> max_extent;
    double simplify_distance;
    bool process_all_rings;
    std::string image_format;
    mapnik::scaling_method_e scaling_method;
    std::launch threading_mode;
    std::string error_name;
    Napi::FunctionReference cb;
} vector_tile_composite_baton_t;
*/

/**
 * Composite an array of vector tiles into one vector tile
 *
 * @name composite
 * @memberof VectorTile
 * @instance
 * @param {Array<mapnik.VectorTile>} array - an array of vector tile objects
 * @param {object} [options]
 * @param {float} [options.scale_factor=1.0]
 * @param {number} [options.offset_x=0]
 * @param {number} [options.offset_y=0]
 * @param {float} [options.area_threshold=0.1] - used to discard small polygons.
 * If a value is greater than `0` it will trigger polygons with an area smaller
 * than the value to be discarded. Measured in grid integers, not spherical mercator
 * coordinates.
 * @param {boolean} [options.strictly_simple=true] - ensure all geometry is valid according to
 * OGC Simple definition
 * @param {boolean} [options.multi_polygon_union=false] - union all multipolygons
 * @param {Object<mapnik.polygonFillType>} [options.fill_type=mapnik.polygonFillType.positive]
 * the fill type used in determining what are holes and what are outer rings. See the
 * [Clipper documentation](http://www.angusj.com/delphi/clipper/documentation/Docs/Units/ClipperLib/Types/PolyFillType.htm)
 * to learn more about fill types.
 * @param {float} [options.scale_denominator=0.0]
 * @param {boolean} [options.reencode=false]
 * @param {Array<number>} [options.max_extent=minx,miny,maxx,maxy]
 * @param {float} [options.simplify_distance=0.0] - Simplification works to generalize
 * geometries before encoding into vector tiles.simplification distance The
 * `simplify_distance` value works in integer space over a 4096 pixel grid and uses
 * the [Douglas-Peucker algorithm](https://en.wikipedia.org/wiki/Ramer%E2%80%93Douglas%E2%80%93Peucker_algorithm).
 * @param {boolean} [options.process_all_rings=false] - if `true`, don't assume winding order and ring order of
 * polygons are correct according to the [`2.0` Mapbox Vector Tile specification](https://github.com/mapbox/vector-tile-spec)
 * @param {string} [options.image_format=webp] or `jpeg`, `png`, `tiff`
 * @param {string} [options.scaling_method=bilinear] - can be any
 * of the <mapnik.imageScaling> methods
 * @param {string} [options.threading_mode=deferred]
 * @param {Function} callback - `function(err)`
 * @example
 * var vt1 = new mapnik.VectorTile(0,0,0);
 * var vt2 = new mapnik.VectorTile(0,0,0);
 * var options = {
 *   scale: 1.0,
 *   offset_x: 0,
 *   offset_y: 0,
 *   area_threshold: 0.1,
 *   strictly_simple: false,
 *   multi_polygon_union: true,
 *   fill_type: mapnik.polygonFillType.nonZero,
 *   process_all_rings:false,
 *   scale_denominator: 0.0,
 *   reencode: true
 * }
 * // add vt2 to vt1 tile
 * vt1.composite([vt2], options, function(err) {
 *   if (err) throw err;
 *   // your custom code with `vt1`
 * });
 *
 */
Napi::Value VectorTile::composite(Napi::CallbackInfo const& info)
{
    Napi::Env env = info.Env();
    return env.Undefined();
    /*
    if ((info.Length() < 2) || !info[info.Length()-1]->IsFunction())
    {
        return _compositeSync(info);
        return;
    }
    if (!info[0].IsArray())
    {
        Napi::TypeError::New(env, "must provide an array of VectorTile objects and an optional options object").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    Napi::Array vtiles = info[0].As<Napi::Array>();
    unsigned num_tiles = vtiles->Length();
    if (num_tiles < 1)
    {
        Napi::TypeError::New(env, "must provide an array with at least one VectorTile object and an optional options object").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    // options needed for re-rendering tiles
    // unclear yet to what extent these need to be user
    // driven, but we expose here to avoid hardcoding
    double scale_factor = 1.0;
    unsigned offset_x = 0;
    unsigned offset_y = 0;
    double area_threshold = 0.1;
    bool strictly_simple = true;
    bool multi_polygon_union = false;
    mapnik::vector_tile_impl::polygon_fill_type fill_type = mapnik::vector_tile_impl::positive_fill;
    double scale_denominator = 0.0;
    bool reencode = false;
    boost::optional<mapnik::box2d<double>> max_extent;
    double simplify_distance = 0.0;
    bool process_all_rings = false;
    std::string image_format = "webp";
    mapnik::scaling_method_e scaling_method = mapnik::SCALING_BILINEAR;
    std::launch threading_mode = std::launch::deferred;
    std::string merc_srs("+init=epsg:3857");

    if (info.Length() > 2)
    {
        // options object
        if (!info[1].IsObject())
        {
            Napi::TypeError::New(env, "optional second argument must be an options object").ThrowAsJavaScriptException();
            return env.Undefined();
        }
        Napi::Object options = info[1].ToObject(Napi::GetCurrentContext());
        if ((options).Has(Napi::String::New(env, "area_threshold")).FromMaybe(false))
        {
            Napi::Value area_thres = (options).Get(Napi::String::New(env, "area_threshold"));
            if (!area_thres.IsNumber())
            {
                Napi::TypeError::New(env, "option 'area_threshold' must be a number").ThrowAsJavaScriptException();
                return env.Undefined();
            }
            area_threshold = area_thres.As<Napi::Number>().DoubleValue();
            if (area_threshold < 0.0)
            {
                Napi::TypeError::New(env, "option 'area_threshold' can not be negative").ThrowAsJavaScriptException();
                return env.Undefined();
            }
        }
        if ((options).Has(Napi::String::New(env, "strictly_simple")).FromMaybe(false))
        {
            Napi::Value strict_simp = (options).Get(Napi::String::New(env, "strictly_simple"));
            if (!strict_simp->IsBoolean())
            {
                Napi::TypeError::New(env, "strictly_simple value must be a boolean").ThrowAsJavaScriptException();
                return env.Undefined();
            }
            strictly_simple = strict_simp.As<Napi::Boolean>().Value();
        }
        if ((options).Has(Napi::String::New(env, "multi_polygon_union")).FromMaybe(false))
        {
            Napi::Value mpu = (options).Get(Napi::String::New(env, "multi_polygon_union"));
            if (!mpu->IsBoolean())
            {
                Napi::TypeError::New(env, "multi_polygon_union value must be a boolean").ThrowAsJavaScriptException();
                return env.Undefined();
            }
            multi_polygon_union = mpu.As<Napi::Boolean>().Value();
        }
        if ((options).Has(Napi::String::New(env, "fill_type")).FromMaybe(false))
        {
            Napi::Value ft = (options).Get(Napi::String::New(env, "fill_type"));
            if (!ft.IsNumber())
            {
                Napi::TypeError::New(env, "optional arg 'fill_type' must be a number").ThrowAsJavaScriptException();
                return env.Undefined();
            }
            fill_type = static_cast<mapnik::vector_tile_impl::polygon_fill_type>(ft.As<Napi::Number>().Int32Value());
            if (fill_type >= mapnik::vector_tile_impl::polygon_fill_type_max)
            {
                Napi::TypeError::New(env, "optional arg 'fill_type' out of possible range").ThrowAsJavaScriptException();
                return env.Undefined();
            }
        }
        if ((options).Has(Napi::String::New(env, "threading_mode")).FromMaybe(false))
        {
            Napi::Value param_val = (options).Get(Napi::String::New(env, "threading_mode"));
            if (!param_val.IsNumber())
            {
                Napi::TypeError::New(env, "option 'threading_mode' must be an unsigned integer").ThrowAsJavaScriptException();
                return env.Undefined();
            }
            threading_mode = static_cast<std::launch>(param_val.As<Napi::Number>().Int32Value());
            if (threading_mode != std::launch::async &&
                threading_mode != std::launch::deferred &&
                threading_mode != (std::launch::async | std::launch::deferred))
            {
                Napi::TypeError::New(env, "optional arg 'threading_mode' is not a valid value").ThrowAsJavaScriptException();
                return env.Undefined();
            }
        }
        if ((options).Has(Napi::String::New(env, "simplify_distance")).FromMaybe(false))
        {
            Napi::Value param_val = (options).Get(Napi::String::New(env, "simplify_distance"));
            if (!param_val.IsNumber())
            {
                Napi::TypeError::New(env, "option 'simplify_distance' must be an floating point number").ThrowAsJavaScriptException();
                return env.Undefined();
            }
            simplify_distance = param_val.As<Napi::Number>().DoubleValue();
            if (simplify_distance < 0.0)
            {
                Napi::TypeError::New(env, "option 'simplify_distance' can not be negative").ThrowAsJavaScriptException();
                return env.Undefined();
            }
        }
        if ((options).Has(Napi::String::New(env, "scale")).FromMaybe(false))
        {
            Napi::Value bind_opt = (options).Get(Napi::String::New(env, "scale"));
            if (!bind_opt.IsNumber())
            {
                Napi::TypeError::New(env, "optional arg 'scale' must be a number").ThrowAsJavaScriptException();
                return env.Undefined();
            }
            scale_factor = bind_opt.As<Napi::Number>().DoubleValue();
            if (scale_factor < 0.0)
            {
                Napi::TypeError::New(env, "option 'scale' can not be negative").ThrowAsJavaScriptException();
                return env.Undefined();
            }
        }
        if ((options).Has(Napi::String::New(env, "scale_denominator")).FromMaybe(false))
        {
            Napi::Value bind_opt = (options).Get(Napi::String::New(env, "scale_denominator"));
            if (!bind_opt.IsNumber())
            {
                Napi::TypeError::New(env, "optional arg 'scale_denominator' must be a number").ThrowAsJavaScriptException();
                return env.Undefined();
            }
            scale_denominator = bind_opt.As<Napi::Number>().DoubleValue();
            if (scale_denominator < 0.0)
            {
                Napi::TypeError::New(env, "option 'scale_denominator' can not be negative").ThrowAsJavaScriptException();
                return env.Undefined();
            }
        }
        if ((options).Has(Napi::String::New(env, "offset_x")).FromMaybe(false))
        {
            Napi::Value bind_opt = (options).Get(Napi::String::New(env, "offset_x"));
            if (!bind_opt.IsNumber())
            {
                Napi::TypeError::New(env, "optional arg 'offset_x' must be a number").ThrowAsJavaScriptException();
                return env.Undefined();
            }
            offset_x = bind_opt.As<Napi::Number>().Int32Value();
        }
        if ((options).Has(Napi::String::New(env, "offset_y")).FromMaybe(false))
        {
            Napi::Value bind_opt = (options).Get(Napi::String::New(env, "offset_y"));
            if (!bind_opt.IsNumber())
            {
                Napi::TypeError::New(env, "optional arg 'offset_y' must be a number").ThrowAsJavaScriptException();
                return env.Undefined();
            }
            offset_y = bind_opt.As<Napi::Number>().Int32Value();
        }
        if ((options).Has(Napi::String::New(env, "reencode")).FromMaybe(false))
        {
            Napi::Value reencode_opt = (options).Get(Napi::String::New(env, "reencode"));
            if (!reencode_opt->IsBoolean())
            {
                Napi::TypeError::New(env, "reencode value must be a boolean").ThrowAsJavaScriptException();
                return env.Undefined();
            }
            reencode = reencode_opt.As<Napi::Boolean>().Value();
        }
        if ((options).Has(Napi::String::New(env, "max_extent")).FromMaybe(false))
        {
            Napi::Value max_extent_opt = (options).Get(Napi::String::New(env, "max_extent"));
            if (!max_extent_opt->IsArray())
            {
                Napi::TypeError::New(env, "max_extent value must be an array of [minx,miny,maxx,maxy]").ThrowAsJavaScriptException();
                return env.Undefined();
            }
            Napi::Array bbox = max_extent_opt.As<Napi::Array>();
            auto len = bbox->Length();
            if (!(len == 4))
            {
                Napi::TypeError::New(env, "max_extent value must be an array of [minx,miny,maxx,maxy]").ThrowAsJavaScriptException();
                return env.Undefined();
            }
            Napi::Value minx = (bbox).Get(0);
            Napi::Value miny = (bbox).Get(1);
            Napi::Value maxx = (bbox).Get(2);
            Napi::Value maxy = (bbox).Get(3);
            if (!minx.IsNumber() || !miny.IsNumber() || !maxx.IsNumber() || !maxy.IsNumber())
            {
                Napi::Error::New(env, "max_extent [minx,miny,maxx,maxy] must be numbers").ThrowAsJavaScriptException();
                return env.Undefined();
            }
            max_extent = mapnik::box2d<double>(minx.As<Napi::Number>().DoubleValue(),miny.As<Napi::Number>().DoubleValue(),
                                               maxx.As<Napi::Number>().DoubleValue(),maxy.As<Napi::Number>().DoubleValue());
        }
        if ((options).Has(Napi::String::New(env, "process_all_rings")).FromMaybe(false))
        {
            Napi::Value param_val = (options).Get(Napi::String::New(env, "process_all_rings"));
            if (!param_val->IsBoolean()) {
                Napi::TypeError::New(env, "option 'process_all_rings' must be a boolean").ThrowAsJavaScriptException();
                return env.Undefined();
            }
            process_all_rings = param_val.As<Napi::Boolean>().Value();
        }

        if ((options).Has(Napi::String::New(env, "image_scaling")).FromMaybe(false))
        {
            Napi::Value param_val = (options).Get(Napi::String::New(env, "image_scaling"));
            if (!param_val.IsString())
            {
                Napi::TypeError::New(env, "option 'image_scaling' must be a string").ThrowAsJavaScriptException();
                return env.Undefined();
            }
            std::string image_scaling = TOSTR(param_val);
            boost::optional<mapnik::scaling_method_e> method = mapnik::scaling_method_from_string(image_scaling);
            if (!method)
            {
                Napi::TypeError::New(env, "option 'image_scaling' must be a string and a valid scaling method (e.g 'bilinear')").ThrowAsJavaScriptException();
                return env.Undefined();
            }
            scaling_method = *method;
        }

        if ((options).Has(Napi::String::New(env, "image_format")).FromMaybe(false))
        {
            Napi::Value param_val = (options).Get(Napi::String::New(env, "image_format"));
            if (!param_val.IsString())
            {
                Napi::TypeError::New(env, "option 'image_format' must be a string").ThrowAsJavaScriptException();
                return env.Undefined();
            }
            image_format = TOSTR(param_val);
        }
    }

    Napi::Value callback = info[info.Length()-1];
    vector_tile_composite_baton_t *closure = new vector_tile_composite_baton_t();
    closure->request.data = closure;
    closure->offset_x = offset_x;
    closure->offset_y = offset_y;
    closure->strictly_simple = strictly_simple;
    closure->fill_type = fill_type;
    closure->multi_polygon_union = multi_polygon_union;
    closure->area_threshold = area_threshold;
    closure->scale_factor = scale_factor;
    closure->scale_denominator = scale_denominator;
    closure->reencode = reencode;
    closure->max_extent = max_extent;
    closure->simplify_distance = simplify_distance;
    closure->process_all_rings = process_all_rings;
    closure->scaling_method = scaling_method;
    closure->image_format = image_format;
    closure->threading_mode = threading_mode;
    closure->d = info.Holder().Unwrap<VectorTile>();
    closure->error = false;
    closure->vtiles.reserve(num_tiles);
    for (unsigned j=0;j < num_tiles;++j)
    {
        Napi::Value val = (vtiles).Get(j);
        if (!val.IsObject())
        {
            delete closure;
            Napi::TypeError::New(env, "must provide an array of VectorTile objects").ThrowAsJavaScriptException();
            return env.Undefined();
        }
        Napi::Object tile_obj = val->ToObject(Napi::GetCurrentContext());
        if (tile_obj->IsNull() || tile_obj->IsUndefined() || !Napi::New(env, VectorTile::constructor)->HasInstance(tile_obj))
        {
            delete closure;
            Napi::TypeError::New(env, "must provide an array of VectorTile objects").ThrowAsJavaScriptException();
            return env.Undefined();
        }
        VectorTile* vt = tile_obj.Unwrap<VectorTile>();
        vt->Ref();
        closure->vtiles.push_back(vt);
    }
    closure->d->Ref();
    closure->cb.Reset(callback.As<Napi::Function>());
    uv_queue_work(uv_default_loop(), &closure->request, EIO_Composite, (uv_after_work_cb)EIO_AfterComposite);
    return;
    */
}
/*
void VectorTile::EIO_Composite(uv_work_t* req)
{
    vector_tile_composite_baton_t *closure = static_cast<vector_tile_composite_baton_t *>(req->data);
    try
    {
        _composite(closure->d,
                   closure->vtiles,
                   closure->scale_factor,
                   closure->offset_x,
                   closure->offset_y,
                   closure->area_threshold,
                   closure->strictly_simple,
                   closure->multi_polygon_union,
                   closure->fill_type,
                   closure->scale_denominator,
                   closure->reencode,
                   closure->max_extent,
                   closure->simplify_distance,
                   closure->process_all_rings,
                   closure->image_format,
                   closure->scaling_method,
                   closure->threading_mode);
    }
    catch (std::exception const& ex)
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
}

void VectorTile::EIO_AfterComposite(uv_work_t* req)
{
    Napi::HandleScope scope(env);
    Napi::AsyncResource async_resource(__func__);
    vector_tile_composite_baton_t *closure = static_cast<vector_tile_composite_baton_t *>(req->data);

    if (closure->error)
    {
        Napi::Value argv[1] = { Napi::Error::New(env, closure->error_name.c_str()) };
        async_resource.runInAsyncScope(Napi::GetCurrentContext()->Global(), Napi::New(env, closure->cb), 1, argv);
    }
    else
    {
        Napi::Value argv[2] = { env.Undefined(), closure->d->handle() };
        async_resource.runInAsyncScope(Napi::GetCurrentContext()->Global(), Napi::New(env, closure->cb), 2, argv);
    }
    for (VectorTile* vt : closure->vtiles)
    {
        vt->Unref();
    }
    closure->d->Unref();
    closure->cb.Reset();
    delete closure;
}
*/

/**
 * Get the extent of this vector tile
 *
 * @memberof VectorTile
 * @instance
 * @name extent
 * @returns {Array<number>} array of extent in the form of `[minx,miny,maxx,maxy]`
 * @example
 * var vt = new mapnik.VectorTile(9,112,195);
 * var extent = vt.extent();
 * console.log(extent); // [-11271098.44281895, 4696291.017841229, -11192826.925854929, 4774562.534805248]
 */
Napi::Value VectorTile::extent(Napi::CallbackInfo const& info)
{
    Napi::Env env = info.Env();
    Napi::EscapableHandleScope scope(env);
    Napi::Array arr = Napi::Array::New(env, 4);
    mapnik::box2d<double> const& e = tile_->extent();
    arr.Set(0u, Napi::Number::New(env, e.minx()));
    arr.Set(1u, Napi::Number::New(env, e.miny()));
    arr.Set(2u, Napi::Number::New(env, e.maxx()));
    arr.Set(3u, Napi::Number::New(env, e.maxy()));
    return scope.Escape(arr);

}

/**
 * Get the extent including the buffer of this vector tile
 *
 * @memberof VectorTile
 * @instance
 * @name bufferedExtent
 * @returns {Array<number>} extent - `[minx, miny, maxx, maxy]`
 * @example
 * var vt = new mapnik.VectorTile(9,112,195);
 * var extent = vt.bufferedExtent();
 * console.log(extent); // [-11273544.4277, 4693845.0329, -11190380.9409, 4777008.5197];
 */
Napi::Value VectorTile::bufferedExtent(Napi::CallbackInfo const& info)
{
    Napi::Env env = info.Env();
    Napi::EscapableHandleScope scope(env);
    Napi::Array arr = Napi::Array::New(env, 4);
    mapnik::box2d<double> e = tile_->get_buffered_extent();
    arr.Set(0u, Napi::Number::New(env, e.minx()));
    arr.Set(1u, Napi::Number::New(env, e.miny()));
    arr.Set(2u, Napi::Number::New(env, e.maxx()));
    arr.Set(3u, Napi::Number::New(env, e.maxy()));
    return scope.Escape(arr);
}

/**
 * Get the names of all of the layers in this vector tile
 *
 * @memberof VectorTile
 * @instance
 * @name names
 * @returns {Array<string>} layer names
 * @example
 * var vt = new mapnik.VectorTile(0,0,0);
 * var data = fs.readFileSync('./path/to/data.mvt');
 * vt.addDataSync(data);
 * console.log(vt.names()); // ['layer-name', 'another-layer']
 */
Napi::Value VectorTile::names(Napi::CallbackInfo const& info)
{
    Napi::Env env = info.Env();
    Napi::EscapableHandleScope scope(env);
    std::vector<std::string> const& names = tile_->get_layers();
    Napi::Array arr = Napi::Array::New(env, names.size());
    std::size_t idx = 0;
    for (std::string const& name : names)
    {
        arr.Set(idx++, name);
    }
    return scope.Escape(arr);
}

/**
 * Extract the layer by a given name to a new vector tile
 *
 * @memberof VectorTile
 * @instance
 * @name layer
 * @param {string} layer_name - name of layer
 * @returns {mapnik.VectorTile} mapnik VectorTile object
 * @example
 * var vt = new mapnik.VectorTile(0,0,0);
 * var data = fs.readFileSync('./path/to/data.mvt');
 * vt.addDataSync(data);
 * console.log(vt.names()); // ['layer-name', 'another-layer']
 * var vt2 = vt.layer('layer-name');
 * console.log(vt2.names()); // ['layer-name']
 */
Napi::Value VectorTile::layer(Napi::CallbackInfo const& info)
{
    Napi::Env env = info.Env();
    Napi::EscapableHandleScope scope(env);

    return env.Undefined();
    /*
    if (info.Length() < 1)
    {
        Napi::Error::New(env, "first argument must be either a layer name").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    Napi::Value layer_id = info[0];
    std::string layer_name;
    if (!layer_id.IsString())
    {
        Napi::TypeError::New(env, "'layer' argument must be a layer name (string)").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    layer_name = TOSTR(layer_id);
    VectorTile* d = info.Holder().Unwrap<VectorTile>();
    if (!d->get_tile()->has_layer(layer_name))
    {
        Napi::TypeError::New(env, "layer does not exist in vector tile").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    VectorTile* v = new VectorTile(d->get_tile()->z(), d->get_tile()->x(), d->get_tile()->y(), d->tile_size(), d->buffer_size());
    protozero::pbf_reader tile_message(d->get_tile()->get_reader());
    while (tile_message.next(mapnik::vector_tile_impl::Tile_Encoding::LAYERS))
    {
        auto data_view = tile_message.get_view();
        protozero::pbf_reader layer_message(data_view);
        if (!layer_message.next(mapnik::vector_tile_impl::Layer_Encoding::NAME))
        {
            continue;
        }
        std::string name = layer_message.get_string();
        if (layer_name == name)
        {
            v->get_tile()->append_layer_buffer(data_view.data(), data_view.size(), layer_name);
            break;
        }
    }
    Napi::Value ext = Napi::External::New(env, v);
    Napi::MaybeLocal<v8::Object> maybe_local = Napi::NewInstance(Napi::GetFunction(Napi::New(env, constructor)), 1, &ext);
    if (maybe_local.IsEmpty()) Napi::Error::New(env, "Could not create new Layer instance").ThrowAsJavaScriptException();

    else return maybe_local;
    return;
    */
}

/**
 * Get the names of all of the empty layers in this vector tile
 *
 * @memberof VectorTile
 * @instance
 * @name emptyLayers
 * @returns {Array<string>} layer names
 * @example
 * var vt = new mapnik.VectorTile(0,0,0);
 * var empty = vt.emptyLayers();
 * // assumes you have added data to your tile
 * console.log(empty); // ['layer-name', 'empty-layer']
 */
Napi::Value VectorTile::emptyLayers(Napi::CallbackInfo const& info)
{
    Napi::Env env = info.Env();
    Napi::EscapableHandleScope scope(env);
    std::set<std::string> const& names = tile_->get_empty_layers();
    Napi::Array arr = Napi::Array::New(env, names.size());
    std::size_t idx = 0;
    for (std::string const& name : names)
    {
        arr.Set(idx++, name);
    }
    return scope.Escape(arr);
}

/**
 * Get the names of all of the painted layers in this vector tile. "Painted" is
 * a check to see if data exists in the source dataset in a tile.
 *
 * @memberof VectorTile
 * @instance
 * @name paintedLayers
 * @returns {Array<string>} layer names
 * @example
 * var vt = new mapnik.VectorTile(0,0,0);
 * var painted = vt.paintedLayers();
 * // assumes you have added data to your tile
 * console.log(painted); // ['layer-name']
 */
Napi::Value VectorTile::paintedLayers(Napi::CallbackInfo const& info)
{
    Napi::Env env = info.Env();
    Napi::EscapableHandleScope scope(env);
    std::set<std::string> const& names = tile_->get_painted_layers();
    Napi::Array arr = Napi::Array::New(env, names.size());
    std::size_t idx = 0;
    for (std::string const& name : names)
    {
        arr.Set(idx++, name);
    }
    return scope.Escape(arr);
}

/**
 * Return whether this vector tile is empty - whether it has no
 * layers and no features
 *
 * @memberof VectorTile
 * @instance
 * @name empty
 * @returns {boolean} whether the layer is empty
 * @example
 * var vt = new mapnik.VectorTile(0,0,0);
 * var empty = vt.empty();
 * console.log(empty); // true
 */
Napi::Value VectorTile::empty(Napi::CallbackInfo const& info)
{
    return Napi::Boolean::New(info.Env(), tile_->is_empty());
}

/**
 * Get whether the vector tile has been painted. "Painted" is
 * a check to see if data exists in the source dataset in a tile.
 *
 * @memberof VectorTile
 * @instance
 * @name painted
 * @returns {boolean} painted
 * @example
 * var vt = new mapnik.VectorTile(0,0,0);
 * var painted = vt.painted();
 * console.log(painted); // false
 */
Napi::Value VectorTile::painted(Napi::CallbackInfo const& info)
{
    return Napi::Boolean::New(info.Env(), tile_->is_painted());
}


/**
 * Add a {@link Image} as a tile layer (synchronous)
 *
 * @memberof VectorTile
 * @instance
 * @name addImageSync
 * @param {mapnik.Image} image
 * @param {string} name of the layer to be added
 * @param {Object} options
 * @param {string} [options.image_scaling=bilinear] can be any
 * of the <mapnik.imageScaling> methods
 * @param {string} [options.image_format=webp] or `jpeg`, `png`, `tiff`
 * @example
 * var vt = new mapnik.VectorTile(1, 0, 0, {
 *   tile_size:256
 * });
 * var im = new mapnik.Image(256, 256);
 * vt.addImageSync(im, 'layer-name', {
 *   image_format: 'jpeg',
 *   image_scaling: 'gaussian'
 * });
 */
Napi::Value VectorTile::addImageSync(Napi::CallbackInfo const& info)
{
    Napi::Env env = info.Env();
    Napi::EscapableHandleScope scope(env);
    return env.Undefined();
    /*
    VectorTile* d = info.Holder().Unwrap<VectorTile>();
    if (info.Length() < 1 || !info[0].IsObject())
    {
        Napi::Error::New(env, "first argument must be an Image object").ThrowAsJavaScriptException();

        return scope.Escape(env.Undefined());
    }
    if (info.Length() < 2 || !info[1].IsString())
    {
        Napi::Error::New(env, "second argument must be a layer name (string)").ThrowAsJavaScriptException();

        return scope.Escape(env.Undefined());
    }
    std::string layer_name = TOSTR(info[1]);
    Napi::Object obj = info[0].ToObject(Napi::GetCurrentContext());
    if (obj->IsNull() ||
        obj->IsUndefined() ||
        !Napi::New(env, Image::constructor)->HasInstance(obj))
    {
        Napi::Error::New(env, "first argument must be an Image object").ThrowAsJavaScriptException();

        return scope.Escape(env.Undefined());
    }
    Image *im = obj.Unwrap<Image>();
    if (im->get()->width() <= 0 || im->get()->height() <= 0)
    {
        Napi::Error::New(env, "Image width and height must be greater then zero").ThrowAsJavaScriptException();

        return scope.Escape(env.Undefined());
    }

    std::string image_format = "webp";
    mapnik::scaling_method_e scaling_method = mapnik::SCALING_BILINEAR;

    if (info.Length() > 2)
    {
        // options object
        if (!info[2].IsObject())
        {
            Napi::Error::New(env, "optional third argument must be an options object").ThrowAsJavaScriptException();

            return scope.Escape(env.Undefined());
        }

        Napi::Object options = info[2].ToObject(Napi::GetCurrentContext());
        if ((options).Has(Napi::String::New(env, "image_scaling")).FromMaybe(false))
        {
            Napi::Value param_val = (options).Get(Napi::String::New(env, "image_scaling"));
            if (!param_val.IsString())
            {
                Napi::TypeError::New(env, "option 'image_scaling' must be a string").ThrowAsJavaScriptException();

                return scope.Escape(env.Undefined());
            }
            std::string image_scaling = TOSTR(param_val);
            boost::optional<mapnik::scaling_method_e> method = mapnik::scaling_method_from_string(image_scaling);
            if (!method)
            {
                Napi::TypeError::New(env, "option 'image_scaling' must be a string and a valid scaling method (e.g 'bilinear')").ThrowAsJavaScriptException();

                return scope.Escape(env.Undefined());
            }
            scaling_method = *method;
        }

        if ((options).Has(Napi::String::New(env, "image_format")).FromMaybe(false))
        {
            Napi::Value param_val = (options).Get(Napi::String::New(env, "image_format"));
            if (!param_val.IsString())
            {
                Napi::TypeError::New(env, "option 'image_format' must be a string").ThrowAsJavaScriptException();

                return scope.Escape(env.Undefined());
            }
            image_format = TOSTR(param_val);
        }
    }
    mapnik::image_any im_copy = *im->get();
    std::shared_ptr<mapnik::memory_datasource> ds = std::make_shared<mapnik::memory_datasource>(mapnik::parameters());
    mapnik::raster_ptr ras = std::make_shared<mapnik::raster>(d->get_tile()->extent(), im_copy, 1.0);
    mapnik::context_ptr ctx = std::make_shared<mapnik::context_type>();
    mapnik::feature_ptr feature(mapnik::feature_factory::create(ctx,1));
    feature->set_raster(ras);
    ds->push(feature);
    ds->envelope(); // can be removed later, currently doesn't work with out this.
    ds->set_envelope(d->get_tile()->extent());
    try
    {
        // create map object
        mapnik::Map map(d->tile_size(),d->tile_size(),"+init=epsg:3857");
        mapnik::layer lyr(layer_name,"+init=epsg:3857");
        lyr.set_datasource(ds);
        map.add_layer(lyr);

        mapnik::vector_tile_impl::processor ren(map);
        ren.set_scaling_method(scaling_method);
        ren.set_image_format(image_format);
        ren.update_tile(*d->get_tile());
        return env.True();
    }
    catch (std::exception const& ex)
    {
        Napi::Error::New(env, ex.what()).ThrowAsJavaScriptException();

        return scope.Escape(env.Undefined());
    }
    return scope.Escape(env.Undefined());
    */
}
/*
typedef struct
{
    uv_work_t request;
    VectorTile* d;
    Image* im;
    std::string layer_name;
    std::string image_format;
    mapnik::scaling_method_e scaling_method;
    bool error;
    std::string error_name;
    Napi::FunctionReference cb;
} vector_tile_add_image_baton_t;
*/
/**
 * Add a <mapnik.Image> as a tile layer (asynchronous)
 *
 * @memberof VectorTile
 * @instance
 * @name addImage
 * @param {mapnik.Image} image
 * @param {string} name of the layer to be added
 * @param {Object} [options]
 * @param {string} [options.image_scaling=bilinear] can be any
 * of the <mapnik.imageScaling> methods
 * @param {string} [options.image_format=webp] other options include `jpeg`, `png`, `tiff`
 * @example
 * var vt = new mapnik.VectorTile(1, 0, 0, {
 *   tile_size:256
 * });
 * var im = new mapnik.Image(256, 256);
 * vt.addImage(im, 'layer-name', {
 *   image_format: 'jpeg',
 *   image_scaling: 'gaussian'
 * }, function(err) {
 *   if (err) throw err;
 *   // your custom code using `vt`
 * });
 */
Napi::Value VectorTile::addImage(Napi::CallbackInfo const& info)
{
    Napi::Env env = info.Env();
    return env.Undefined();
/*

    // If last param is not a function assume sync
    if (info.Length() < 2)
    {
        Napi::Error::New(env, "addImage requires at least two parameters: an Image and a layer name").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    Napi::Value callback = info[info.Length() - 1];
    if (!info[info.Length() - 1]->IsFunction())
    {
        return _addImageSync(info);
        return;
    }
    VectorTile* d = this;
    if (!info[0].IsObject())
    {
        Napi::Error::New(env, "first argument must be an Image object").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    if (!info[1].IsString())
    {
        Napi::Error::New(env, "second argument must be a layer name (string)").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    std::string layer_name = TOSTR(info[1]);
    Napi::Object obj = info[0].ToObject(Napi::GetCurrentContext());
    if (obj->IsNull() ||
        obj->IsUndefined() ||
        !Napi::New(env, Image::constructor)->HasInstance(obj))
    {
        Napi::Error::New(env, "first argument must be an Image object").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    Image *im = obj.Unwrap<Image>();
    if (im->get()->width() <= 0 || im->get()->height() <= 0)
    {
        Napi::Error::New(env, "Image width and height must be greater then zero").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    std::string image_format = "webp";
    mapnik::scaling_method_e scaling_method = mapnik::SCALING_BILINEAR;

    if (info.Length() > 3)
    {
        // options object
        if (!info[2].IsObject())
        {
            Napi::Error::New(env, "optional third argument must be an options object").ThrowAsJavaScriptException();
            return env.Undefined();
        }

        Napi::Object options = info[2].ToObject(Napi::GetCurrentContext());
        if ((options).Has(Napi::String::New(env, "image_scaling")).FromMaybe(false))
        {
            Napi::Value param_val = (options).Get(Napi::String::New(env, "image_scaling"));
            if (!param_val.IsString())
            {
                Napi::TypeError::New(env, "option 'image_scaling' must be a string").ThrowAsJavaScriptException();
                return env.Undefined();
            }
            std::string image_scaling = TOSTR(param_val);
            boost::optional<mapnik::scaling_method_e> method = mapnik::scaling_method_from_string(image_scaling);
            if (!method)
            {
                Napi::TypeError::New(env, "option 'image_scaling' must be a string and a valid scaling method (e.g 'bilinear')").ThrowAsJavaScriptException();
                return env.Undefined();
            }
            scaling_method = *method;
        }

        if ((options).Has(Napi::String::New(env, "image_format")).FromMaybe(false))
        {
            Napi::Value param_val = (options).Get(Napi::String::New(env, "image_format"));
            if (!param_val.IsString())
            {
                Napi::TypeError::New(env, "option 'image_format' must be a string").ThrowAsJavaScriptException();
                return env.Undefined();
            }
            image_format = TOSTR(param_val);
        }
    }
    vector_tile_add_image_baton_t *closure = new vector_tile_add_image_baton_t();
    closure->request.data = closure;
    closure->d = d;
    closure->im = im;
    closure->scaling_method = scaling_method;
    closure->image_format = image_format;
    closure->layer_name = layer_name;
    closure->error = false;
    closure->cb.Reset(callback.As<Napi::Function>());
    uv_queue_work(uv_default_loop(), &closure->request, EIO_AddImage, (uv_after_work_cb)EIO_AfterAddImage);
    d->Ref();
    im->Ref();
    return;
*/
}
/*
void VectorTile::EIO_AddImage(uv_work_t* req)
{
    vector_tile_add_image_baton_t *closure = static_cast<vector_tile_add_image_baton_t *>(req->data);

    try
    {
        mapnik::image_any im_copy = *closure->im->get();
        std::shared_ptr<mapnik::memory_datasource> ds = std::make_shared<mapnik::memory_datasource>(mapnik::parameters());
        mapnik::raster_ptr ras = std::make_shared<mapnik::raster>(closure->d->get_tile()->extent(), im_copy, 1.0);
        mapnik::context_ptr ctx = std::make_shared<mapnik::context_type>();
        mapnik::feature_ptr feature(mapnik::feature_factory::create(ctx,1));
        feature->set_raster(ras);
        ds->push(feature);
        ds->envelope(); // can be removed later, currently doesn't work with out this.
        ds->set_envelope(closure->d->get_tile()->extent());
        // create map object
        mapnik::Map map(closure->d->tile_size(),closure->d->tile_size(),"+init=epsg:3857");
        mapnik::layer lyr(closure->layer_name,"+init=epsg:3857");
        lyr.set_datasource(ds);
        map.add_layer(lyr);

        mapnik::vector_tile_impl::processor ren(map);
        ren.set_scaling_method(closure->scaling_method);
        ren.set_image_format(closure->image_format);
        ren.update_tile(*closure->d->get_tile());
    }
    catch (std::exception const& ex)
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
}

void VectorTile::EIO_AfterAddImage(uv_work_t* req)
{
    Napi::HandleScope scope(env);
    Napi::AsyncResource async_resource(__func__);
    vector_tile_add_image_baton_t *closure = static_cast<vector_tile_add_image_baton_t *>(req->data);
    if (closure->error)
    {
        Napi::Value argv[1] = { Napi::Error::New(env, closure->error_name.c_str()) };
        async_resource.runInAsyncScope(Napi::GetCurrentContext()->Global(), Napi::New(env, closure->cb), 1, argv);
    }
    else
    {
        Napi::Value argv[1] = { env.Undefined() };
        async_resource.runInAsyncScope(Napi::GetCurrentContext()->Global(), Napi::New(env, closure->cb), 1, argv);
    }

    closure->d->Unref();
    closure->im->Unref();
    closure->cb.Reset();
    delete closure;
}
*/

/**
 * Add raw image buffer as a new tile layer (synchronous)
 *
 * @memberof VectorTile
 * @instance
 * @name addImageBufferSync
 * @param {Buffer} buffer - raw data
 * @param {string} name - name of the layer to be added
 * @example
 * var vt = new mapnik.VectorTile(1, 0, 0, {
 *   tile_size: 256
 * });
 * var image_buffer = fs.readFileSync('./path/to/image.jpg');
 * vt.addImageBufferSync(image_buffer, 'layer-name');
 */
Napi::Value VectorTile::addImageBufferSync(Napi::CallbackInfo const& info)
{
    Napi::Env env = info.Env();
    Napi::EscapableHandleScope scope(env);
    return env.Undefined();
/*
    VectorTile* d = info.Holder().Unwrap<VectorTile>();
    if (info.Length() < 1 || !info[0].IsObject())
    {
        Napi::TypeError::New(env, "first argument must be a buffer object").ThrowAsJavaScriptException();

        return scope.Escape(env.Undefined());
    }
    if (info.Length() < 2 || !info[1].IsString())
    {
        Napi::Error::New(env, "second argument must be a layer name (string)").ThrowAsJavaScriptException();

        return scope.Escape(env.Undefined());
    }
    std::string layer_name = TOSTR(info[1]);
    Napi::Object obj = info[0].ToObject(Napi::GetCurrentContext());
    if (!obj.IsBuffer())
    {
        Napi::TypeError::New(env, "first arg must be a buffer object").ThrowAsJavaScriptException();

        return scope.Escape(env.Undefined());
    }
    std::size_t buffer_size = obj.As<Napi::Buffer<char>>().Length();
    if (buffer_size <= 0)
    {
        Napi::Error::New(env, "cannot accept empty buffer as protobuf").ThrowAsJavaScriptException();

        return scope.Escape(env.Undefined());
    }
    try
    {
        add_image_buffer_as_tile_layer(*d->get_tile(), layer_name, obj.As<Napi::Buffer<char>>().Data(), buffer_size);
    }
    catch (std::exception const& ex)
    {
        // no obvious way to get this to throw in JS under obvious conditions
        // but keep the standard exeption cache in C++
        // LCOV_EXCL_START
        Napi::Error::New(env, ex.what()).ThrowAsJavaScriptException();

        return scope.Escape(env.Undefined());
        // LCOV_EXCL_STOP
    }
    return scope.Escape(env.Undefined());
*/
}
/*
typedef struct
{
    uv_work_t request;
    VectorTile* d;
    const char *data;
    size_t dataLength;
    std::string layer_name;
    bool error;
    std::string error_name;
    Napi::FunctionReference cb;
    Napi::Persistent<v8::Object> buffer;
} vector_tile_addimagebuffer_baton_t;
*/

/**
 * Add an encoded image buffer as a layer
 *
 * @memberof VectorTile
 * @instance
 * @name addImageBuffer
 * @param {Buffer} buffer - raw image data
 * @param {string} name - name of the layer to be added
 * @param {Function} callback
 * @example
 * var vt = new mapnik.VectorTile(1, 0, 0, {
 *   tile_size: 256
 * });
 * var image_buffer = fs.readFileSync('./path/to/image.jpg'); // returns a buffer
 * vt.addImageBufferSync(image_buffer, 'layer-name', function(err) {
 *   if (err) throw err;
 *   // your custom code
 * });
 */
Napi::Value VectorTile::addImageBuffer(Napi::CallbackInfo const& info)
{
    Napi::Env env = info.Env();
     //Napi::EscapableHandleScope scope(env);
    return env.Undefined();
    /*
    if (info.Length() < 3)
    {
        return _addImageBufferSync(info);
        return;
    }

    // ensure callback is a function
    Napi::Value callback = info[info.Length() - 1];
    if (!info[info.Length() - 1]->IsFunction())
    {
        Napi::TypeError::New(env, "last argument must be a callback function").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (info.Length() < 1 || !info[0].IsObject())
    {
        Napi::TypeError::New(env, "first argument must be a buffer object").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    if (info.Length() < 2 || !info[1].IsString())
    {
        Napi::Error::New(env, "second argument must be a layer name (string)").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    std::string layer_name = TOSTR(info[1]);
    Napi::Object obj = info[0].ToObject(Napi::GetCurrentContext());
    if (!obj.IsBuffer())
    {
        Napi::TypeError::New(env, "first arg must be a buffer object").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    VectorTile* d = info.Holder().Unwrap<VectorTile>();

    vector_tile_addimagebuffer_baton_t *closure = new vector_tile_addimagebuffer_baton_t();
    closure->request.data = closure;
    closure->d = d;
    closure->layer_name = layer_name;
    closure->error = false;
    closure->cb.Reset(callback.As<Napi::Function>());
    closure->buffer.Reset(obj.As<Napi::Object>());
    closure->data = obj.As<Napi::Buffer<char>>().Data();
    closure->dataLength = obj.As<Napi::Buffer<char>>().Length();
    uv_queue_work(uv_default_loop(), &closure->request, EIO_AddImageBuffer, (uv_after_work_cb)EIO_AfterAddImageBuffer);
    d->Ref();
    return;
    */
}
/*
void VectorTile::EIO_AddImageBuffer(uv_work_t* req)
{
    vector_tile_addimagebuffer_baton_t *closure = static_cast<vector_tile_addimagebuffer_baton_t *>(req->data);

    try
    {
        add_image_buffer_as_tile_layer(*closure->d->get_tile(), closure->layer_name, closure->data, closure->dataLength);
    }
    catch (std::exception const& ex)
    {
        // LCOV_EXCL_START
        closure->error = true;
        closure->error_name = ex.what();
        // LCOV_EXCL_STOP
    }
}

void VectorTile::EIO_AfterAddImageBuffer(uv_work_t* req)
{
    Napi::HandleScope scope(env);
    Napi::AsyncResource async_resource(__func__);
    vector_tile_addimagebuffer_baton_t *closure = static_cast<vector_tile_addimagebuffer_baton_t *>(req->data);
    if (closure->error)
    {
        // LCOV_EXCL_START
        Napi::Value argv[1] = { Napi::Error::New(env, closure->error_name.c_str()) };
        async_resource.runInAsyncScope(Napi::GetCurrentContext()->Global(), Napi::New(env, closure->cb), 1, argv);
        // LCOV_EXCL_STOP
    }
    else
    {
        Napi::Value argv[1] = { env.Undefined() };
        async_resource.runInAsyncScope(Napi::GetCurrentContext()->Global(), Napi::New(env, closure->cb), 1, argv);
    }

    closure->d->Unref();
    closure->cb.Reset();
    closure->buffer.Reset();
    delete closure;
}
*/

struct dummy_surface {};

using surface_type = mapnik::util::variant
    <dummy_surface,
     Image *,
     CairoSurface *
#if defined(GRID_RENDERER)
     ,Grid *
#endif
     >;

struct ref_visitor
{
    void operator() (dummy_surface) {} // no-op
    template <typename SurfaceType>
    void operator() (SurfaceType * surface)
    {
        if (surface != nullptr)
        {
            surface->Ref();
        }
    }
};


struct deref_visitor
{
    void operator() (dummy_surface) {} // no-op
    template <typename SurfaceType>
    void operator() (SurfaceType * surface)
    {
        if (surface != nullptr)
        {
            surface->Unref();
        }
    }
};
/*
struct vector_tile_render_baton_t
{
    uv_work_t request;
    Map* m;
    VectorTile * d;
    surface_type surface;
    mapnik::attributes variables;
    std::string error_name;
    Napi::FunctionReference cb;
    std::string result;
    std::size_t layer_idx;
    std::int64_t z;
    std::int64_t x;
    std::int64_t y;
    unsigned width;
    unsigned height;
    int buffer_size;
    double scale_factor;
    double scale_denominator;
    bool use_cairo;
    bool zxy_override;
    bool error;
    vector_tile_render_baton_t() :
        request(),
        m(nullptr),
        d(nullptr),
        surface(),
        variables(),
        error_name(),
        cb(),
        result(),
        layer_idx(0),
        z(0),
        x(0),
        y(0),
        width(0),
        height(0),
        buffer_size(0),
        scale_factor(1.0),
        scale_denominator(0.0),
        use_cairo(true),
        zxy_override(false),
        error(false)
        {}
};
*/
/*
struct baton_guard
{
    baton_guard(vector_tile_render_baton_t * baton) :
      baton_(baton),
      released_(false) {}

    ~baton_guard()
    {
        if (!released_) delete baton_;
    }

    void release()
    {
        released_ = true;
    }

    vector_tile_render_baton_t * baton_;
    bool released_;
};
*/
/**
 * Render/write this vector tile to a surface/image, like a {@link Image}
 *
 * @name render
 * @memberof VectorTile
 * @instance
 * @param {mapnik.Map} map - mapnik map object
 * @param {mapnik.Image} surface - renderable surface object
 * @param {Object} [options]
 * @param {number} [options.z] an integer zoom level. Must be used with `x` and `y`
 * @param {number} [options.x] an integer x coordinate. Must be used with `y` and `z`.
 * @param {number} [options.y] an integer y coordinate. Must be used with `x` and `z`
 * @param {number} [options.buffer_size] the size of the tile's buffer
 * @param {number} [options.scale] floating point scale factor size to used
 * for rendering
 * @param {number} [options.scale_denominator] An floating point `scale_denominator`
 * to be used by Mapnik when matching zoom filters. If provided this overrides the
 * auto-calculated scale_denominator that is based on the map dimensions and bbox.
 * Do not set this option unless you know what it means.
 * @param {Object} [options.variables] Mapnik 3.x ONLY: A javascript object
 * containing key value pairs that should be passed into Mapnik as variables
 * for rendering and for datasource queries. For example if you passed
 * `vtile.render(map,image,{ variables : {zoom:1} },cb)` then the `@zoom`
 * variable would be usable in Mapnik symbolizers like `line-width:"@zoom"`
 * and as a token in Mapnik postgis sql sub-selects like
 * `(select * from table where some_field > @zoom)` as tmp
 * @param {string} [options.renderer] must be `cairo` or `svg`
 * @param {string|number} [options.layer] option required for grid rendering
 * and must be either a layer name (string) or layer index (integer)
 * @param {Array<string>} [options.fields] must be an array of strings
 * @param {Function} callback
 * @example
 * var vt = new mapnik.VectorTile(0,0,0);
 * var tileSize = vt.tileSize;
 * var map = new mapnik.Map(tileSize, tileSize);
 * vt.render(map, new mapnik.Image(256,256), function(err, image) {
 *   if (err) throw err;
 *   // save the rendered image to an existing image file somewhere
 *   // see mapnik.Image for available methods
 *   image.save('./path/to/image/file.png', 'png32');
 * });
 */
Napi::Value VectorTile::render(Napi::CallbackInfo const& info)
{
    Napi::Env env = info.Env();
    return env.Undefined();
    /*
    VectorTile* d = info.Holder().Unwrap<VectorTile>();
    if (info.Length() < 1 || !info[0].IsObject())
    {
        Napi::TypeError::New(env, "mapnik.Map expected as first arg").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    Napi::Object obj = info[0].ToObject(Napi::GetCurrentContext());
    if (!Napi::New(env, Map::constructor)->HasInstance(obj))
    {
        Napi::TypeError::New(env, "mapnik.Map expected as first arg").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    Map *m = obj.Unwrap<Map>();
    if (info.Length() < 2 || !info[1].IsObject())
    {
        Napi::TypeError::New(env, "a renderable mapnik object is expected as second arg").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    Napi::Object im_obj = info[1].ToObject(Napi::GetCurrentContext());

    // ensure callback is a function
    Napi::Value callback = info[info.Length()-1];
    if (!info[info.Length()-1]->IsFunction())
    {
        Napi::TypeError::New(env, "last argument must be a callback function").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    vector_tile_render_baton_t *closure = new vector_tile_render_baton_t();
    baton_guard guard(closure);
    Napi::Object options = Napi::Object::New(env);

    if (info.Length() > 2)
    {
        bool set_x = false;
        bool set_y = false;
        bool set_z = false;
        if (!info[2].IsObject())
        {
            Napi::TypeError::New(env, "optional third argument must be an options object").ThrowAsJavaScriptException();
            return env.Undefined();
        }
        options = info[2].ToObject(Napi::GetCurrentContext());
        if ((options).Has(Napi::String::New(env, "z")).FromMaybe(false))
        {
            Napi::Value bind_opt = (options).Get(Napi::String::New(env, "z"));
            if (!bind_opt.IsNumber())
            {
                Napi::TypeError::New(env, "optional arg 'z' must be a number").ThrowAsJavaScriptException();
                return env.Undefined();
            }
            closure->z = bind_opt.As<Napi::Number>().Int32Value();
            set_z = true;
            closure->zxy_override = true;
        }
        if ((options).Has(Napi::String::New(env, "x")).FromMaybe(false))
        {
            Napi::Value bind_opt = (options).Get(Napi::String::New(env, "x"));
            if (!bind_opt.IsNumber())
            {
                Napi::TypeError::New(env, "optional arg 'x' must be a number").ThrowAsJavaScriptException();
                return env.Undefined();
            }
            closure->x = bind_opt.As<Napi::Number>().Int32Value();
            set_x = true;
            closure->zxy_override = true;
        }
        if ((options).Has(Napi::String::New(env, "y")).FromMaybe(false))
        {
            Napi::Value bind_opt = (options).Get(Napi::String::New(env, "y"));
            if (!bind_opt.IsNumber())
            {
                Napi::TypeError::New(env, "optional arg 'y' must be a number").ThrowAsJavaScriptException();
                return env.Undefined();
            }
            closure->y = bind_opt.As<Napi::Number>().Int32Value();
            set_y = true;
            closure->zxy_override = true;
        }

        if (closure->zxy_override)
        {
            if (!set_z || !set_x || !set_y)
            {
                Napi::TypeError::New(env, "original args 'z', 'x', and 'y' must all be used together").ThrowAsJavaScriptException();
                return env.Undefined();
            }
            if (closure->x < 0 || closure->y < 0 || closure->z < 0)
            {
                Napi::TypeError::New(env, "original args 'z', 'x', and 'y' can not be negative").ThrowAsJavaScriptException();
                return env.Undefined();
            }
            std::int64_t max_at_zoom = pow(2,closure->z);
            if (closure->x >= max_at_zoom)
            {
                Napi::TypeError::New(env, "required parameter x is out of range of possible values based on z value").ThrowAsJavaScriptException();
                return env.Undefined();
            }
            if (closure->y >= max_at_zoom)
            {
                Napi::TypeError::New(env, "required parameter y is out of range of possible values based on z value").ThrowAsJavaScriptException();
                return env.Undefined();
            }
        }

        if ((options).Has(Napi::String::New(env, "buffer_size")).FromMaybe(false))
        {
            Napi::Value bind_opt = (options).Get(Napi::String::New(env, "buffer_size"));
            if (!bind_opt.IsNumber())
            {
                Napi::TypeError::New(env, "optional arg 'buffer_size' must be a number").ThrowAsJavaScriptException();
                return env.Undefined();
            }
            closure->buffer_size = bind_opt.As<Napi::Number>().Int32Value();
        }
        if ((options).Has(Napi::String::New(env, "scale")).FromMaybe(false))
        {
            Napi::Value bind_opt = (options).Get(Napi::String::New(env, "scale"));
            if (!bind_opt.IsNumber())
            {
                Napi::TypeError::New(env, "optional arg 'scale' must be a number").ThrowAsJavaScriptException();
                return env.Undefined();
            }
            closure->scale_factor = bind_opt.As<Napi::Number>().DoubleValue();
        }
        if ((options).Has(Napi::String::New(env, "scale_denominator")).FromMaybe(false))
        {
            Napi::Value bind_opt = (options).Get(Napi::String::New(env, "scale_denominator"));
            if (!bind_opt.IsNumber())
            {
                Napi::TypeError::New(env, "optional arg 'scale_denominator' must be a number").ThrowAsJavaScriptException();
                return env.Undefined();
            }
            closure->scale_denominator = bind_opt.As<Napi::Number>().DoubleValue();
        }
        if ((options).Has(Napi::String::New(env, "variables")).FromMaybe(false))
        {
            Napi::Value bind_opt = (options).Get(Napi::String::New(env, "variables"));
            if (!bind_opt.IsObject())
            {
                Napi::TypeError::New(env, "optional arg 'variables' must be an object").ThrowAsJavaScriptException();
                return env.Undefined();
            }
            object_to_container(closure->variables,bind_opt->ToObject(Napi::GetCurrentContext()));
        }
    }

    closure->layer_idx = 0;
    if (Napi::New(env, Image::constructor)->HasInstance(im_obj))
    {
        Image *im = im_obj.Unwrap<Image>();
        closure->width = im->get()->width();
        closure->height = im->get()->height();
        closure->surface = im;
    }
    else if (Napi::New(env, CairoSurface::constructor)->HasInstance(im_obj))
    {
        CairoSurface *c = im_obj.Unwrap<CairoSurface>();
        closure->width = c->width();
        closure->height = c->height();
        closure->surface = c;
        if ((options).Has(Napi::String::New(env, "renderer")).FromMaybe(false))
        {
            Napi::Value renderer = (options).Get(Napi::String::New(env, "renderer"));
            if (!renderer.IsString() )
            {
                Napi::Error::New(env, "'renderer' option must be a string of either 'svg' or 'cairo'").ThrowAsJavaScriptException();
                return env.Undefined();
            }
            std::string renderer_name = TOSTR(renderer);
            if (renderer_name == "cairo")
            {
                closure->use_cairo = true;
            }
            else if (renderer_name == "svg")
            {
                closure->use_cairo = false;
            }
            else
            {
                Napi::Error::New(env, "'renderer' option must be a string of either 'svg' or 'cairo'").ThrowAsJavaScriptException();
                return env.Undefined();
            }
        }
    }
#if defined(GRID_RENDERER)
    else if (Napi::New(env, Grid::constructor)->HasInstance(im_obj))
    {
        Grid *g = im_obj.Unwrap<Grid>();
        closure->width = g->get()->width();
        closure->height = g->get()->height();
        closure->surface = g;

        std::size_t layer_idx = 0;

        // grid requires special options for now
        if (!(options).Has(Napi::String::New(env, "layer")).FromMaybe(false))
        {
            Napi::TypeError::New(env, "'layer' option required for grid rendering and must be either a layer name(string) or layer index (integer)").ThrowAsJavaScriptException();
            return env.Undefined();
        }
        else
        {
            std::vector<mapnik::layer> const& layers = m->get()->layers();
            Napi::Value layer_id = (options).Get(Napi::String::New(env, "layer"));
            if (layer_id.IsString())
            {
                bool found = false;
                unsigned int idx(0);
                std::string layer_name = TOSTR(layer_id);
                for (mapnik::layer const& lyr : layers)
                {
                    if (lyr.name() == layer_name)
                    {
                        found = true;
                        layer_idx = idx;
                        break;
                    }
                    ++idx;
                }
                if (!found)
                {
                    std::ostringstream s;
                    s << "Layer name '" << layer_name << "' not found";
                    Napi::TypeError::New(env, s.str().c_str()).ThrowAsJavaScriptException();
                    return env.Undefined();
                }
            }
            else if (layer_id.IsNumber())
            {
                layer_idx = layer_id.As<Napi::Number>().Int32Value();
                std::size_t layer_num = layers.size();
                if (layer_idx >= layer_num)
                {
                    std::ostringstream s;
                    s << "Zero-based layer index '" << layer_idx << "' not valid, ";
                    if (layer_num > 0)
                    {
                        s << "only '" << layer_num << "' layers exist in map";
                    }
                    else
                    {
                        s << "no layers found in map";
                    }
                    Napi::TypeError::New(env, s.str().c_str()).ThrowAsJavaScriptException();
                    return env.Undefined();
                }
            }
            else
            {
                Napi::TypeError::New(env, "'layer' option required for grid rendering and must be either a layer name(string) or layer index (integer)").ThrowAsJavaScriptException();
                return env.Undefined();
            }
        }
        if ((options).Has(Napi::String::New(env, "fields")).FromMaybe(false))
        {
            Napi::Value param_val = (options).Get(Napi::String::New(env, "fields"));
            if (!param_val->IsArray())
            {
                Napi::TypeError::New(env, "option 'fields' must be an array of strings").ThrowAsJavaScriptException();
                return env.Undefined();
            }
            Napi::Array a = param_val.As<Napi::Array>();
            unsigned int i = 0;
            unsigned int num_fields = a->Length();
            while (i < num_fields)
            {
                Napi::Value name = (a).Get(i);
                if (name.IsString())
                {
                    g->get()->add_field(TOSTR(name));
                }
                ++i;
            }
        }
        closure->layer_idx = layer_idx;
    }
#endif
    else
    {
        Napi::TypeError::New(env, "renderable mapnik object expected as second arg").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    closure->request.data = closure;
    closure->d = d;
    closure->m = m;
    closure->error = false;
    closure->cb.Reset(callback.As<Napi::Function>());
    uv_queue_work(uv_default_loop(), &closure->request, EIO_RenderTile, (uv_after_work_cb)EIO_AfterRenderTile);
    mapnik::util::apply_visitor(ref_visitor(), closure->surface);
    m->Ref();
    d->Ref();
    guard.release();
    return;
    */
}
/*
template <typename Renderer> void process_layers(Renderer & ren,
                                            mapnik::request const& m_req,
                                            mapnik::projection const& map_proj,
                                            std::vector<mapnik::layer> const& layers,
                                            double scale_denom,
                                            std::string const& map_srs,
                                            vector_tile_render_baton_t *closure)
{
    for (auto const& lyr : layers)
    {
        if (lyr.visible(scale_denom))
        {
            protozero::pbf_reader layer_msg;
            if (closure->d->get_tile()->layer_reader(lyr.name(), layer_msg))
            {
                mapnik::layer lyr_copy(lyr);
                lyr_copy.set_srs(map_srs);
                std::shared_ptr<mapnik::vector_tile_impl::tile_datasource_pbf> ds = std::make_shared<
                                                mapnik::vector_tile_impl::tile_datasource_pbf>(
                                                    layer_msg,
                                                    closure->d->get_tile()->x(),
                                                    closure->d->get_tile()->y(),
                                                    closure->d->get_tile()->z());
                ds->set_envelope(m_req.get_buffered_extent());
                lyr_copy.set_datasource(ds);
                std::set<std::string> names;
                ren.apply_to_layer(lyr_copy,
                                   ren,
                                   map_proj,
                                   m_req.scale(),
                                   scale_denom,
                                   m_req.width(),
                                   m_req.height(),
                                   m_req.extent(),
                                   m_req.buffer_size(),
                                   names);
            }
        }
    }
}
*/
/*
void VectorTile::EIO_RenderTile(uv_work_t* req)
{
    vector_tile_render_baton_t *closure = static_cast<vector_tile_render_baton_t *>(req->data);

    try
    {
        mapnik::Map const& map_in = *closure->m->get();
        mapnik::box2d<double> map_extent;
        if (closure->zxy_override)
        {
            map_extent = mapnik::vector_tile_impl::tile_mercator_bbox(closure->x,closure->y,closure->z);
        }
        else
        {
            map_extent = mapnik::vector_tile_impl::tile_mercator_bbox(closure->d->get_tile()->x(),
                                                                      closure->d->get_tile()->y(),
                                                                      closure->d->get_tile()->z());
        }
        mapnik::request m_req(closure->width, closure->height, map_extent);
        m_req.set_buffer_size(closure->buffer_size);
        mapnik::projection map_proj(map_in.srs(),true);
        double scale_denom = closure->scale_denominator;
        if (scale_denom <= 0.0)
        {
            scale_denom = mapnik::scale_denominator(m_req.scale(),map_proj.is_geographic());
        }
        scale_denom *= closure->scale_factor;
        std::vector<mapnik::layer> const& layers = map_in.layers();
#if defined(GRID_RENDERER)
        // render grid for layer
        if (closure->surface.is<Grid *>())
        {
            Grid * g = mapnik::util::get<Grid *>(closure->surface);
            mapnik::grid_renderer<mapnik::grid> ren(map_in,
                                                    m_req,
                                                    closure->variables,
                                                    *(g->get()),
                                                    closure->scale_factor);
            ren.start_map_processing(map_in);

            mapnik::layer const& lyr = layers[closure->layer_idx];
            if (lyr.visible(scale_denom))
            {
                protozero::pbf_reader layer_msg;
                if (closure->d->get_tile()->layer_reader(lyr.name(),layer_msg))
                {
                    // copy field names
                    std::set<std::string> attributes = g->get()->get_fields();

                    // todo - make this a static constant
                    std::string known_id_key = "__id__";
                    if (attributes.find(known_id_key) != attributes.end())
                    {
                        attributes.erase(known_id_key);
                    }
                    std::string join_field = g->get()->get_key();
                    if (known_id_key != join_field &&
                        attributes.find(join_field) == attributes.end())
                    {
                        attributes.insert(join_field);
                    }

                    mapnik::layer lyr_copy(lyr);
                    lyr_copy.set_srs(map_in.srs());
                    std::shared_ptr<mapnik::vector_tile_impl::tile_datasource_pbf> ds = std::make_shared<
                                                    mapnik::vector_tile_impl::tile_datasource_pbf>(
                                                        layer_msg,
                                                        closure->d->get_tile()->x(),
                                                        closure->d->get_tile()->y(),
                                                        closure->d->get_tile()->z());
                    ds->set_envelope(m_req.get_buffered_extent());
                    lyr_copy.set_datasource(ds);
                    ren.apply_to_layer(lyr_copy,
                                       ren,
                                       map_proj,
                                       m_req.scale(),
                                       scale_denom,
                                       m_req.width(),
                                       m_req.height(),
                                       m_req.extent(),
                                       m_req.buffer_size(),
                                       attributes);
                }
                ren.end_map_processing(map_in);
            }
        }
        else
#endif
        if (closure->surface.is<CairoSurface *>())
        {
            CairoSurface * c = mapnik::util::get<CairoSurface *>(closure->surface);
            if (closure->use_cairo)
            {
#if defined(HAVE_CAIRO)
                mapnik::cairo_surface_ptr surface;
                // TODO - support any surface type
                surface = mapnik::cairo_surface_ptr(cairo_svg_surface_create_for_stream(
                                                       (cairo_write_func_t)c->write_callback,
                                                       (void*)(&c->ss_),
                                                       static_cast<double>(c->width()),
                                                       static_cast<double>(c->height())
                                                    ),mapnik::cairo_surface_closer());
                mapnik::cairo_ptr c_context = mapnik::create_context(surface);
                mapnik::cairo_renderer<mapnik::cairo_ptr> ren(map_in,m_req,
                                                                closure->variables,
                                                                c_context,closure->scale_factor);
                ren.start_map_processing(map_in);
                process_layers(ren,m_req,map_proj,layers,scale_denom,map_in.srs(),closure);
                ren.end_map_processing(map_in);
#else
                closure->error = true;
                closure->error_name = "no support for rendering svg with cairo backend";
#endif
            }
            else
            {
#if defined(SVG_RENDERER)
                typedef mapnik::svg_renderer<std::ostream_iterator<char> > svg_ren;
                std::ostream_iterator<char> output_stream_iterator(c->ss_);
                svg_ren ren(map_in, m_req,
                            closure->variables,
                            output_stream_iterator, closure->scale_factor);
                ren.start_map_processing(map_in);
                process_layers(ren,m_req,map_proj,layers,scale_denom,map_in.srs(),closure);
                ren.end_map_processing(map_in);
#else
                closure->error = true;
                closure->error_name = "no support for rendering svg with native svg backend (-DSVG_RENDERER)";
#endif
            }
        }
        // render all layers with agg
        else if (closure->surface.is<Image *>())
        {
            Image * js_image = mapnik::util::get<Image *>(closure->surface);
            mapnik::image_any & im = *(js_image->get());
            if (im.is<mapnik::image_rgba8>())
            {
                mapnik::image_rgba8 & im_data = mapnik::util::get<mapnik::image_rgba8>(im);
                mapnik::agg_renderer<mapnik::image_rgba8> ren(map_in,m_req,
                                                        closure->variables,
                                                        im_data,closure->scale_factor);
                ren.start_map_processing(map_in);
                process_layers(ren,m_req,map_proj,layers,scale_denom,map_in.srs(),closure);
                ren.end_map_processing(map_in);
            }
            else
            {
                throw std::runtime_error("This image type is not currently supported for rendering.");
            }
        }
    }
    catch (std::exception const& ex)
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
}

void VectorTile::EIO_AfterRenderTile(uv_work_t* req)
{
    Napi::HandleScope scope(env);
    Napi::AsyncResource async_resource(__func__);
    vector_tile_render_baton_t *closure = static_cast<vector_tile_render_baton_t *>(req->data);
    if (closure->error)
    {
        Napi::Value argv[1] = { Napi::Error::New(env, closure->error_name.c_str()) };
        async_resource.runInAsyncScope(Napi::GetCurrentContext()->Global(), Napi::New(env, closure->cb), 1, argv);
    }
    else
    {
        if (closure->surface.is<Image *>())
        {
            Napi::Value argv[2] = { env.Undefined(), mapnik::util::get<Image *>(closure->surface)->handle() };
            async_resource.runInAsyncScope(Napi::GetCurrentContext()->Global(), Napi::New(env, closure->cb), 2, argv);
        }
#if defined(GRID_RENDERER)
        else if (closure->surface.is<Grid *>())
        {
            Napi::Value argv[2] = { env.Undefined(), mapnik::util::get<Grid *>(closure->surface)->handle() };
            async_resource.runInAsyncScope(Napi::GetCurrentContext()->Global(), Napi::New(env, closure->cb), 2, argv);
        }
#endif
        else if (closure->surface.is<CairoSurface *>())
        {
            Napi::Value argv[2] = { env.Undefined(), mapnik::util::get<CairoSurface *>(closure->surface)->handle() };
            async_resource.runInAsyncScope(Napi::GetCurrentContext()->Global(), Napi::New(env, closure->cb), 2, argv);
        }
    }

    mapnik::util::apply_visitor(deref_visitor(), closure->surface);
    closure->m->Unref();
    closure->d->Unref();
    closure->cb.Reset();
    delete closure;
}
*/

/**
 * Remove all data from this vector tile (synchronously)
 * @name clearSync
 * @memberof VectorTile
 * @instance
 * @example
 * vt.clearSync();
 * console.log(vt.getData().length); // 0
 */
Napi::Value VectorTile::clearSync(Napi::CallbackInfo const& info)
{
    Napi::Env env = info.Env();
    Napi::EscapableHandleScope scope(env);
    return env.Undefined();

    //VectorTile* d = info.Holder().Unwrap<VectorTile>();
    //d->clear();
    //return scope.Escape(env.Undefined());
}
/*
typedef struct
{
    uv_work_t request;
    VectorTile* d;
    std::string format;
    bool error;
    std::string error_name;
    Napi::FunctionReference cb;
} clear_vector_tile_baton_t;
*/

/**
 * Remove all data from this vector tile
 *
 * @memberof VectorTile
 * @instance
 * @name clear
 * @param {Function} callback
 * @example
 * vt.clear(function(err) {
 *   if (err) throw err;
 *   console.log(vt.getData().length); // 0
 * });
 */

Napi::Value VectorTile::clear(Napi::CallbackInfo const& info)
{
    Napi::Env env = info.Env();
    //Napi::EscapableHandleScope scope(env);
    return env.Undefined();
/*
    VectorTile* d = info.Holder().Unwrap<VectorTile>();

    if (info.Length() == 0)
    {
        return _clearSync(info);
        return;
    }
    // ensure callback is a function
    Napi::Value callback = info[info.Length() - 1];
    if (!callback->IsFunction())
    {
        Napi::TypeError::New(env, "last argument must be a callback function").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    clear_vector_tile_baton_t *closure = new clear_vector_tile_baton_t();
    closure->request.data = closure;
    closure->d = d;
    closure->error = false;
    closure->cb.Reset(callback.As<Napi::Function>());
    uv_queue_work(uv_default_loop(), &closure->request, EIO_Clear, (uv_after_work_cb)EIO_AfterClear);
    d->Ref();
    return;
*/
}
/*
void VectorTile::EIO_Clear(uv_work_t* req)
{
    clear_vector_tile_baton_t *closure = static_cast<clear_vector_tile_baton_t *>(req->data);
    try
    {
        closure->d->clear();
    }
    catch(std::exception const& ex)
    {
        // No reason this should ever throw an exception, not currently testable.
        // LCOV_EXCL_START
        closure->error = true;
        closure->error_name = ex.what();
        // LCOV_EXCL_STOP
    }
}

void VectorTile::EIO_AfterClear(uv_work_t* req)
{
    Napi::HandleScope scope(env);
    Napi::AsyncResource async_resource(__func__);
    clear_vector_tile_baton_t *closure = static_cast<clear_vector_tile_baton_t *>(req->data);
    if (closure->error)
    {
        // No reason this should ever throw an exception, not currently testable.
        // LCOV_EXCL_START
        Napi::Value argv[1] = { Napi::Error::New(env, closure->error_name.c_str()) };
        async_resource.runInAsyncScope(Napi::GetCurrentContext()->Global(), Napi::New(env, closure->cb), 1, argv);
        // LCOV_EXCL_STOP
    }
    else
    {
        Napi::Value argv[1] = { env.Undefined() };
        async_resource.runInAsyncScope(Napi::GetCurrentContext()->Global(), Napi::New(env, closure->cb), 1, argv);
    }
    closure->d->Unref();
    closure->cb.Reset();
    delete closure;
}
*/
#if BOOST_VERSION >= 105800

// LCOV_EXCL_START
struct not_simple_feature
{
    not_simple_feature(std::string const& layer_,
                       std::int64_t feature_id_)
        : layer(layer_),
          feature_id(feature_id_) {}
    std::string const layer;
    std::int64_t const feature_id;
};
// LCOV_EXCL_STOP

struct not_valid_feature
{
    not_valid_feature(std::string const& message_,
                      std::string const& layer_,
                      std::int64_t feature_id_,
                      std::string const& geojson_)
        : message(message_),
          layer(layer_),
          feature_id(feature_id_),
          geojson(geojson_) {}
    std::string const message;
    std::string const layer;
    std::int64_t const feature_id;
    std::string const geojson;
};

void layer_not_simple(protozero::pbf_reader const& layer_msg,
               unsigned x,
               unsigned y,
               unsigned z,
               std::vector<not_simple_feature> & errors)
{
    mapnik::vector_tile_impl::tile_datasource_pbf ds(layer_msg, x, y, z);
    mapnik::query q(mapnik::box2d<double>(std::numeric_limits<double>::lowest(),
                                          std::numeric_limits<double>::lowest(),
                                          std::numeric_limits<double>::max(),
                                          std::numeric_limits<double>::max()));
    mapnik::layer_descriptor ld = ds.get_descriptor();
    for (auto const& item : ld.get_descriptors())
    {
        q.add_property_name(item.get_name());
    }
    mapnik::featureset_ptr fs = ds.features(q);
    if (fs && mapnik::is_valid(fs))
    {
        mapnik::feature_ptr feature;
        while ((feature = fs->next()))
        {
            if (!mapnik::geometry::is_simple(feature->get_geometry()))
            {
                // Right now we don't have an obvious way of bypassing our validation
                // process in JS, so let's skip testing this line
                // LCOV_EXCL_START
                errors.emplace_back(ds.get_name(), feature->id());
                // LCOV_EXCL_STOP
            }
        }
    }
}

struct visitor_geom_valid
{
    std::vector<not_valid_feature> & errors;
    mapnik::feature_ptr & feature;
    std::string const& layer_name;
    bool split_multi_features;

    visitor_geom_valid(std::vector<not_valid_feature> & errors_,
                       mapnik::feature_ptr & feature_,
                       std::string const& layer_name_,
                       bool split_multi_features_)
        : errors(errors_),
          feature(feature_),
          layer_name(layer_name_),
          split_multi_features(split_multi_features_) {}

    void operator() (mapnik::geometry::geometry_empty const&) {}

    template <typename T>
    void operator() (mapnik::geometry::point<T> const& geom)
    {
        std::string message;
        if (!mapnik::geometry::is_valid(geom, message))
        {
            if (!mapnik::geometry::is_valid(geom, message))
            {
                mapnik::feature_impl feature_new(feature->context(),feature->id());
                std::string result;
                std::string feature_str;
                result += "{\"type\":\"FeatureCollection\",\"features\":[";
                feature_new.set_data(feature->get_data());
                feature_new.set_geometry(mapnik::geometry::geometry<T>(geom));
                if (!mapnik::util::to_geojson(feature_str, feature_new))
                {
                    // LCOV_EXCL_START
                    throw std::runtime_error("Failed to generate GeoJSON geometry");
                    // LCOV_EXCL_STOP
                }
                result += feature_str;
                result += "]}";
                errors.emplace_back(message,
                                    layer_name,
                                    feature->id(),
                                    result);
            }
        }
    }

    template <typename T>
    void operator() (mapnik::geometry::multi_point<T> const& geom)
    {
        std::string message;
        if (!mapnik::geometry::is_valid(geom, message))
        {
            if (!mapnik::geometry::is_valid(geom, message))
            {
                mapnik::feature_impl feature_new(feature->context(),feature->id());
                std::string result;
                std::string feature_str;
                result += "{\"type\":\"FeatureCollection\",\"features\":[";
                feature_new.set_data(feature->get_data());
                feature_new.set_geometry(mapnik::geometry::geometry<T>(geom));
                if (!mapnik::util::to_geojson(feature_str, feature_new))
                {
                    // LCOV_EXCL_START
                    throw std::runtime_error("Failed to generate GeoJSON geometry");
                    // LCOV_EXCL_STOP
                }
                result += feature_str;
                result += "]}";
                errors.emplace_back(message,
                                    layer_name,
                                    feature->id(),
                                    result);
            }
        }
    }

    template <typename T>
    void operator() (mapnik::geometry::line_string<T> const& geom)
    {
        std::string message;
        if (!mapnik::geometry::is_valid(geom, message))
        {
            if (!mapnik::geometry::is_valid(geom, message))
            {
                mapnik::feature_impl feature_new(feature->context(),feature->id());
                std::string result;
                std::string feature_str;
                result += "{\"type\":\"FeatureCollection\",\"features\":[";
                feature_new.set_data(feature->get_data());
                feature_new.set_geometry(mapnik::geometry::geometry<T>(geom));
                if (!mapnik::util::to_geojson(feature_str, feature_new))
                {
                    // LCOV_EXCL_START
                    throw std::runtime_error("Failed to generate GeoJSON geometry");
                    // LCOV_EXCL_STOP
                }
                result += feature_str;
                result += "]}";
                errors.emplace_back(message,
                                    layer_name,
                                    feature->id(),
                                    result);
            }
        }
    }

    template <typename T>
    void operator() (mapnik::geometry::multi_line_string<T> const& geom)
    {
        if (split_multi_features)
        {
            for (auto const& ls : geom)
            {
                std::string message;
                if (!mapnik::geometry::is_valid(ls, message))
                {
                    mapnik::feature_impl feature_new(feature->context(),feature->id());
                    std::string result;
                    std::string feature_str;
                    result += "{\"type\":\"FeatureCollection\",\"features\":[";
                    feature_new.set_data(feature->get_data());
                    feature_new.set_geometry(mapnik::geometry::geometry<T>(ls));
                    if (!mapnik::util::to_geojson(feature_str, feature_new))
                    {
                        // LCOV_EXCL_START
                        throw std::runtime_error("Failed to generate GeoJSON geometry");
                        // LCOV_EXCL_STOP
                    }
                    result += feature_str;
                    result += "]}";
                    errors.emplace_back(message,
                                        layer_name,
                                        feature->id(),
                                        result);
                }
            }
        }
        else
        {
            std::string message;
            if (!mapnik::geometry::is_valid(geom, message))
            {
                mapnik::feature_impl feature_new(feature->context(),feature->id());
                std::string result;
                std::string feature_str;
                result += "{\"type\":\"FeatureCollection\",\"features\":[";
                feature_new.set_data(feature->get_data());
                feature_new.set_geometry(mapnik::geometry::geometry<T>(geom));
                if (!mapnik::util::to_geojson(feature_str, feature_new))
                {
                    // LCOV_EXCL_START
                    throw std::runtime_error("Failed to generate GeoJSON geometry");
                    // LCOV_EXCL_STOP
                }
                result += feature_str;
                result += "]}";
                errors.emplace_back(message,
                                    layer_name,
                                    feature->id(),
                                    result);
            }
        }
    }

    template <typename T>
    void operator() (mapnik::geometry::polygon<T> const& geom)
    {
        std::string message;
        if (!mapnik::geometry::is_valid(geom, message))
        {
            if (!mapnik::geometry::is_valid(geom, message))
            {
                mapnik::feature_impl feature_new(feature->context(),feature->id());
                std::string result;
                std::string feature_str;
                result += "{\"type\":\"FeatureCollection\",\"features\":[";
                feature_new.set_data(feature->get_data());
                feature_new.set_geometry(mapnik::geometry::geometry<T>(geom));
                if (!mapnik::util::to_geojson(feature_str, feature_new))
                {
                    // LCOV_EXCL_START
                    throw std::runtime_error("Failed to generate GeoJSON geometry");
                    // LCOV_EXCL_STOP
                }
                result += feature_str;
                result += "]}";
                errors.emplace_back(message,
                                    layer_name,
                                    feature->id(),
                                    result);
            }
        }
    }

    template <typename T>
    void operator() (mapnik::geometry::multi_polygon<T> const& geom)
    {
        if (split_multi_features)
        {
            for (auto const& poly : geom)
            {
                std::string message;
                if (!mapnik::geometry::is_valid(poly, message))
                {
                    mapnik::feature_impl feature_new(feature->context(),feature->id());
                    std::string result;
                    std::string feature_str;
                    result += "{\"type\":\"FeatureCollection\",\"features\":[";
                    feature_new.set_data(feature->get_data());
                    feature_new.set_geometry(mapnik::geometry::geometry<T>(poly));
                    if (!mapnik::util::to_geojson(feature_str, feature_new))
                    {
                        // LCOV_EXCL_START
                        throw std::runtime_error("Failed to generate GeoJSON geometry");
                        // LCOV_EXCL_STOP
                    }
                    result += feature_str;
                    result += "]}";
                    errors.emplace_back(message,
                                        layer_name,
                                        feature->id(),
                                        result);
                }
            }
        }
        else
        {
            std::string message;
            if (!mapnik::geometry::is_valid(geom, message))
            {
                mapnik::feature_impl feature_new(feature->context(),feature->id());
                std::string result;
                std::string feature_str;
                result += "{\"type\":\"FeatureCollection\",\"features\":[";
                feature_new.set_data(feature->get_data());
                feature_new.set_geometry(mapnik::geometry::geometry<T>(geom));
                if (!mapnik::util::to_geojson(feature_str, feature_new))
                {
                    // LCOV_EXCL_START
                    throw std::runtime_error("Failed to generate GeoJSON geometry");
                    // LCOV_EXCL_STOP
                }
                result += feature_str;
                result += "]}";
                errors.emplace_back(message,
                                    layer_name,
                                    feature->id(),
                                    result);
            }
        }
    }

    template <typename T>
    void operator() (mapnik::geometry::geometry_collection<T> const& geom)
    {
        // This should never be able to be reached.
        // LCOV_EXCL_START
        for (auto const& g : geom)
        {
            mapnik::util::apply_visitor((*this), g);
        }
        // LCOV_EXCL_STOP
    }
};

void layer_not_valid(protozero::pbf_reader & layer_msg,
               unsigned x,
               unsigned y,
               unsigned z,
               std::vector<not_valid_feature> & errors,
               bool split_multi_features = false,
               bool lat_lon = false,
               bool web_merc = false)
{
    if (web_merc || lat_lon)
    {
        mapnik::vector_tile_impl::tile_datasource_pbf ds(layer_msg, x, y, z);
        mapnik::query q(mapnik::box2d<double>(std::numeric_limits<double>::lowest(),
                                              std::numeric_limits<double>::lowest(),
                                              std::numeric_limits<double>::max(),
                                              std::numeric_limits<double>::max()));
        mapnik::layer_descriptor ld = ds.get_descriptor();
        for (auto const& item : ld.get_descriptors())
        {
            q.add_property_name(item.get_name());
        }
        mapnik::featureset_ptr fs = ds.features(q);
        if (fs && mapnik::is_valid(fs))
        {
            mapnik::feature_ptr feature;
            while ((feature = fs->next()))
            {
                if (lat_lon)
                {
                    mapnik::projection wgs84("+init=epsg:4326",true);
                    mapnik::projection merc("+init=epsg:3857",true);
                    mapnik::proj_transform prj_trans(merc,wgs84);
                    unsigned int n_err = 0;
                    mapnik::util::apply_visitor(
                            visitor_geom_valid(errors, feature, ds.get_name(), split_multi_features),
                            mapnik::geometry::reproject_copy(feature->get_geometry(), prj_trans, n_err));
                }
                else
                {
                    mapnik::util::apply_visitor(
                            visitor_geom_valid(errors, feature, ds.get_name(), split_multi_features),
                            feature->get_geometry());
                }
            }
        }
    }
    else
    {
        std::vector<protozero::pbf_reader> layer_features;
        std::uint32_t version = 1;
        std::string layer_name;
        while (layer_msg.next())
        {
            switch (layer_msg.tag())
            {
                case mapnik::vector_tile_impl::Layer_Encoding::NAME:
                    layer_name = layer_msg.get_string();
                    break;
                case mapnik::vector_tile_impl::Layer_Encoding::FEATURES:
                    layer_features.push_back(layer_msg.get_message());
                    break;
                case mapnik::vector_tile_impl::Layer_Encoding::VERSION:
                    version = layer_msg.get_uint32();
                    break;
                default:
                    layer_msg.skip();
                    break;
            }
        }
        for (auto feature_msg : layer_features)
        {
            mapnik::vector_tile_impl::GeometryPBF::pbf_itr geom_itr;
            bool has_geom = false;
            bool has_geom_type = false;
            std::int32_t geom_type_enum = 0;
            std::uint64_t feature_id = 0;
            while (feature_msg.next())
            {
                switch (feature_msg.tag())
                {
                    case mapnik::vector_tile_impl::Feature_Encoding::ID:
                        feature_id = feature_msg.get_uint64();
                        break;
                    case mapnik::vector_tile_impl::Feature_Encoding::TYPE:
                        geom_type_enum = feature_msg.get_enum();
                        has_geom_type = true;
                        break;
                    case mapnik::vector_tile_impl::Feature_Encoding::GEOMETRY:
                        geom_itr = feature_msg.get_packed_uint32();
                        has_geom = true;
                        break;
                    default:
                        feature_msg.skip();
                        break;
                }
            }
            if (has_geom && has_geom_type)
            {
                // Decode the geometry first into an int64_t mapnik geometry
                mapnik::context_ptr ctx = std::make_shared<mapnik::context_type>();
                mapnik::feature_ptr feature(mapnik::feature_factory::create(ctx,1));
                mapnik::vector_tile_impl::GeometryPBF geoms(geom_itr);
                feature->set_geometry(mapnik::vector_tile_impl::decode_geometry<double>(geoms, geom_type_enum, version, 0.0, 0.0, 1.0, 1.0));
                mapnik::util::apply_visitor(
                        visitor_geom_valid(errors, feature, layer_name, split_multi_features),
                        feature->get_geometry());
            }
        }
    }
}

void vector_tile_not_simple(mapnik::vector_tile_impl::merc_tile_ptr const& tile,
                            std::vector<not_simple_feature> & errors)
{
    protozero::pbf_reader tile_msg(tile->get_reader());
    while (tile_msg.next(mapnik::vector_tile_impl::Tile_Encoding::LAYERS))
    {
        protozero::pbf_reader layer_msg(tile_msg.get_message());
        layer_not_simple(layer_msg,
                         tile->x(),
                         tile->y(),
                         tile->z(),
                         errors);
    }
}

Napi::Array make_not_simple_array(Napi::Env env, std::vector<not_simple_feature> & errors)
{
    Napi::Array array = Napi::Array::New(env, errors.size());
    Napi::String layer_key = Napi::String::New(env, "layer");
    Napi::String feature_id_key = Napi::String::New(env, "featureId");
    std::uint32_t idx = 0;
    for (auto const& error : errors)
    {
        // LCOV_EXCL_START
        Napi::Object obj = Napi::Object::New(env);
        obj.Set(layer_key, Napi::String::New(env, error.layer));
        obj.Set(feature_id_key, Napi::Number::New(env, error.feature_id));
        array.Set(idx++, obj);
        // LCOV_EXCL_STOP
    }
    return array;
}

void vector_tile_not_valid(mapnik::vector_tile_impl::merc_tile_ptr const& tile,
                           std::vector<not_valid_feature> & errors,
                           bool split_multi_features = false,
                           bool lat_lon = false,
                           bool web_merc = false)
{
    protozero::pbf_reader tile_msg(tile->get_reader());
    while (tile_msg.next(mapnik::vector_tile_impl::Tile_Encoding::LAYERS))
    {
        protozero::pbf_reader layer_msg(tile_msg.get_message());
        layer_not_valid(layer_msg,
                        tile->x(),
                        tile->y(),
                        tile->z(),
                        errors,
                        split_multi_features,
                        lat_lon,
                        web_merc);
    }
}

Napi::Array make_not_valid_array(Napi::Env env, std::vector<not_valid_feature> & errors)
{
    Napi::Array array = Napi::Array::New(env, errors.size());
    Napi::String layer_key = Napi::String::New(env, "layer");
    Napi::String feature_id_key = Napi::String::New(env, "featureId");
    Napi::String message_key = Napi::String::New(env, "message");
    Napi::String geojson_key = Napi::String::New(env, "geojson");
    std::size_t idx = 0;
    for (auto const& error : errors)
    {
        Napi::Object obj = Napi::Object::New(env);
        obj.Set(layer_key, Napi::String::New(env, error.layer));
        obj.Set(message_key, Napi::String::New(env, error.message));
        obj.Set(feature_id_key, Napi::Number::New(env, error.feature_id));
        obj.Set(geojson_key, Napi::String::New(env, error.geojson));
        array.Set(idx++, obj);
    }
    return array;
}

/*
struct not_simple_baton
{
    uv_work_t request;
    VectorTile* v;
    bool error;
    std::vector<not_simple_feature> result;
    std::string err_msg;
    Napi::FunctionReference cb;
};

struct not_valid_baton
{
    uv_work_t request;
    VectorTile* v;
    bool error;
    bool split_multi_features;
    bool lat_lon;
    bool web_merc;
    std::vector<not_valid_feature> result;
    std::string err_msg;
    Napi::FunctionReference cb;
};
*/
/**
 * Count the number of geometries that are not [OGC simple]{@link http://www.iso.org/iso/catalogue_detail.htm?csnumber=40114}
 *
 * @memberof VectorTile
 * @instance
 * @name reportGeometrySimplicitySync
 * @returns {number} number of features that are not simple
 * @example
 * var simple = vectorTile.reportGeometrySimplicitySync();
 * console.log(simple); // array of non-simple geometries and their layer info
 * console.log(simple.length); // number
 */
Napi::Value VectorTile::reportGeometrySimplicitySync(Napi::CallbackInfo const& info)
{
    Napi::Env env = info.Env();
    Napi::EscapableHandleScope scope(env);
    try
    {
        std::vector<not_simple_feature> errors;
        vector_tile_not_simple(tile_, errors);
        return scope.Escape(make_not_simple_array(env, errors));
    }
    catch (std::exception const& ex)
    {
        // LCOV_EXCL_START
        Napi::Error::New(env, ex.what()).ThrowAsJavaScriptException();

        // LCOV_EXCL_STOP
    }
    // LCOV_EXCL_START
    return env.Undefined();
    // LCOV_EXCL_STOP
}

/**
 * Count the number of geometries that are not [OGC valid]{@link http://postgis.net/docs/using_postgis_dbmanagement.html#OGC_Validity}
 *
 * @memberof VectorTile
 * @instance
 * @name reportGeometryValiditySync
 * @param {object} [options]
 * @param {boolean} [options.split_multi_features=false] - If true does validity checks on multi geometries part by part
 * Normally the validity of multipolygons and multilinestrings is done together against
 * all the parts of the geometries. Changing this to true checks the validity of multipolygons
 * and multilinestrings for each part they contain, rather then as a group.
 * @param {boolean} [options.lat_lon=false] - If true results in EPSG:4326
 * @param {boolean} [options.web_merc=false] - If true results in EPSG:3857
 * @returns {number} number of features that are not valid
 * @example
 * var valid = vectorTile.reportGeometryValiditySync();
 * console.log(valid); // array of invalid geometries and their layer info
 * console.log(valid.length); // number
 */
Napi::Value VectorTile::reportGeometryValiditySync(Napi::CallbackInfo const& info)
{
    Napi::Env env = info.Env();
    Napi::EscapableHandleScope scope(env);
    bool split_multi_features = false;
    bool lat_lon = false;
    bool web_merc = false;
    if (info.Length() >= 1)
    {
        if (!info[0].IsObject())
        {
            Napi::Error::New(env, "The first argument must be an object").ThrowAsJavaScriptException();

            return env.Undefined();
        }
        Napi::Object options = info[0].As<Napi::Object>();

        if (options.Has("split_multi_features"))
        {
            Napi::Value param_val = options.Get("split_multi_features");
            if (!param_val.IsBoolean())
            {
                Napi::Error::New(env, "option 'split_multi_features' must be a boolean").ThrowAsJavaScriptException();
                return env.Undefined();
            }
            split_multi_features = param_val.As<Napi::Boolean>();
        }

        if (options.Has("lat_lon"))
        {
            Napi::Value param_val = options.Get("lat_lon");
            if (!param_val.IsBoolean())
            {
                Napi::Error::New(env, "option 'lat_lon' must be a boolean").ThrowAsJavaScriptException();
                return env.Undefined();
            }
            lat_lon = param_val.As<Napi::Boolean>();
        }

        if (options.Has("web_merc"))
        {
            Napi::Value param_val = options.Get("web_merc");
            if (!param_val.IsBoolean())
            {
                Napi::Error::New(env, "option 'web_merc' must be a boolean").ThrowAsJavaScriptException();
                return env.Undefined();
            }
            web_merc = param_val.As<Napi::Boolean>();
        }
    }

    try
    {
        std::vector<not_valid_feature> errors;
        vector_tile_not_valid(tile_, errors, split_multi_features, lat_lon, web_merc);
        return scope.Escape(make_not_valid_array(env, errors));
    }
    catch (std::exception const& ex)
    {
        Napi::Error::New(env, ex.what()).ThrowAsJavaScriptException();
    }
    return scope.Escape(env.Undefined());
}

/**
 * Count the number of geometries that are not [OGC simple]{@link http://www.iso.org/iso/catalogue_detail.htm?csnumber=40114}
 *
 * @memberof VectorTile
 * @instance
 * @name reportGeometrySimplicity
 * @param {Function} callback
 * @example
 * vectorTile.reportGeometrySimplicity(function(err, simple) {
 *   if (err) throw err;
 *   console.log(simple); // array of non-simple geometries and their layer info
 *   console.log(simple.length); // number
 * });
 */
Napi::Value VectorTile::reportGeometrySimplicity(Napi::CallbackInfo const& info)
{
    Napi::Env env = info.Env();
    //Napi::EscapableHandleScope scope(env);
    /*
    if (info.Length() == 0)
    {
        return reportGeometrySimplicitySync(info);
    }
    // ensure callback is a function
    Napi::Value callback = info[info.Length() - 1];
    if (!callback->IsFunction())
    {
        Napi::TypeError::New(env, "last argument must be a callback function").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    not_simple_baton *closure = new not_simple_baton();
    closure->request.data = closure;
    closure->v = info.Holder().Unwrap<VectorTile>();
    closure->error = false;
    closure->cb.Reset(callback.As<Napi::Function>());
    uv_queue_work(uv_default_loop(), &closure->request, EIO_ReportGeometrySimplicity, (uv_after_work_cb)EIO_AfterReportGeometrySimplicity);
    closure->v->Ref();
    */
    return env.Undefined();

}
/*
void VectorTile::EIO_ReportGeometrySimplicity(uv_work_t* req)
{
    not_simple_baton *closure = static_cast<not_simple_baton *>(req->data);
    try
    {
        vector_tile_not_simple(closure->v, closure->result);
    }
    catch (std::exception const& ex)
    {
        // LCOV_EXCL_START
        closure->error = true;
        closure->err_msg = ex.what();
        // LCOV_EXCL_STOP
    }
}

void VectorTile::EIO_AfterReportGeometrySimplicity(uv_work_t* req)
{
    Napi::HandleScope scope(env);
    Napi::AsyncResource async_resource(__func__);
    not_simple_baton *closure = static_cast<not_simple_baton *>(req->data);
    if (closure->error)
    {
        // LCOV_EXCL_START
        Napi::Value argv[1] = { Napi::Error::New(env, closure->err_msg.c_str()) };
        async_resource.runInAsyncScope(Napi::GetCurrentContext()->Global(), Napi::New(env, closure->cb), 1, argv);
        // LCOV_EXCL_STOP
    }
    else
    {
        Napi::Array array = make_not_simple_array(closure->result);
        Napi::Value argv[2] = { env.Undefined(), array };
        async_resource.runInAsyncScope(Napi::GetCurrentContext()->Global(), Napi::New(env, closure->cb), 2, argv);
    }
    closure->v->Unref();
    closure->cb.Reset();
    delete closure;
}
*/
/**
 * Count the number of geometries that are not [OGC valid]{@link http://postgis.net/docs/using_postgis_dbmanagement.html#OGC_Validity}
 *
 * @memberof VectorTile
 * @instance
 * @name reportGeometryValidity
 * @param {object} [options]
 * @param {boolean} [options.split_multi_features=false] - If true does validity checks on multi geometries part by part
 * Normally the validity of multipolygons and multilinestrings is done together against
 * all the parts of the geometries. Changing this to true checks the validity of multipolygons
 * and multilinestrings for each part they contain, rather then as a group.
 * @param {boolean} [options.lat_lon=false] - If true results in EPSG:4326
 * @param {boolean} [options.web_merc=false] - If true results in EPSG:3857
 * @param {Function} callback
 * @example
 * vectorTile.reportGeometryValidity(function(err, valid) {
 *   console.log(valid); // array of invalid geometries and their layer info
 *   console.log(valid.length); // number
 * });
 */
Napi::Value VectorTile::reportGeometryValidity(Napi::CallbackInfo const& info)
{
    Napi::Env env = info.Env();
    Napi::EscapableHandleScope scope(env);
    return env.Undefined();
    /*
    if (info.Length() == 0 || (info.Length() == 1 && !info[0].IsFunction()))
    {
        return _reportGeometryValiditySync(info);
        return;
    }
    bool split_multi_features = false;
    bool lat_lon = false;
    bool web_merc = false;
    if (info.Length() >= 2)
    {
        if (!info[0].IsObject())
        {
            Napi::Error::New(env, "The first argument must be an object").ThrowAsJavaScriptException();
            return env.Undefined();
        }
        Napi::Object options = info[0].ToObject(Napi::GetCurrentContext());

        if ((options).Has(Napi::String::New(env, "split_multi_features")).FromMaybe(false))
        {
            Napi::Value param_val = (options).Get(Napi::String::New(env, "split_multi_features"));
            if (!param_val->IsBoolean())
            {
                Napi::Error::New(env, "option 'split_multi_features' must be a boolean").ThrowAsJavaScriptException();
                return env.Undefined();
            }
            split_multi_features = param_val.As<Napi::Boolean>().Value();
        }

        if ((options).Has(Napi::String::New(env, "lat_lon")).FromMaybe(false))
        {
            Napi::Value param_val = (options).Get(Napi::String::New(env, "lat_lon"));
            if (!param_val->IsBoolean())
            {
                Napi::Error::New(env, "option 'lat_lon' must be a boolean").ThrowAsJavaScriptException();
                return env.Undefined();
            }
            lat_lon = param_val.As<Napi::Boolean>().Value();
        }

        if ((options).Has(Napi::String::New(env, "web_merc")).FromMaybe(false))
        {
            Napi::Value param_val = (options).Get(Napi::String::New(env, "web_merc"));
            if (!param_val->IsBoolean())
            {
                Napi::Error::New(env, "option 'web_merc' must be a boolean").ThrowAsJavaScriptException();
                return env.Undefined();
            }
            web_merc = param_val.As<Napi::Boolean>().Value();
        }
    }
    // ensure callback is a function
    Napi::Value callback = info[info.Length() - 1];
    if (!callback->IsFunction())
    {
        Napi::TypeError::New(env, "last argument must be a callback function").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    not_valid_baton *closure = new not_valid_baton();
    closure->request.data = closure;
    closure->v = info.Holder().Unwrap<VectorTile>();
    closure->error = false;
    closure->split_multi_features = split_multi_features;
    closure->lat_lon = lat_lon;
    closure->web_merc = web_merc;
    closure->cb.Reset(callback.As<Napi::Function>());
    uv_queue_work(uv_default_loop(), &closure->request, EIO_ReportGeometryValidity, (uv_after_work_cb)EIO_AfterReportGeometryValidity);
    closure->v->Ref();
    return;
    */
}
/*
void VectorTile::EIO_ReportGeometryValidity(uv_work_t* req)
{
    not_valid_baton *closure = static_cast<not_valid_baton *>(req->data);
    try
    {
        vector_tile_not_valid(closure->v, closure->result, closure->split_multi_features, closure->lat_lon, closure->web_merc);
    }
    catch (std::exception const& ex)
    {
        // LCOV_EXCL_START
        closure->error = true;
        closure->err_msg = ex.what();
        // LCOV_EXCL_STOP
    }
}

void VectorTile::EIO_AfterReportGeometryValidity(uv_work_t* req)
{
    Napi::HandleScope scope(env);
    Napi::AsyncResource async_resource(__func__);
    not_valid_baton *closure = static_cast<not_valid_baton *>(req->data);
    if (closure->error)
    {
        // LCOV_EXCL_START
        Napi::Value argv[1] = { Napi::Error::New(env, closure->err_msg.c_str()) };
        async_resource.runInAsyncScope(Napi::GetCurrentContext()->Global(), Napi::New(env, closure->cb), 1, argv);
        // LCOV_EXCL_STOP
    }
    else
    {
        Napi::Array array = make_not_valid_array(closure->result);
        Napi::Value argv[2] = { env.Undefined(), array };
        async_resource.runInAsyncScope(Napi::GetCurrentContext()->Global(), Napi::New(env, closure->cb), 2, argv);
    }
    closure->v->Unref();
    closure->cb.Reset();
    delete closure;
}
*/
#endif // BOOST_VERSION >= 1.58

// accessors
Napi::Value VectorTile::get_tile_x(Napi::CallbackInfo const& info)
{
    return Napi::Number::New(info.Env(), tile_->x());
}

Napi::Value VectorTile::get_tile_y(Napi::CallbackInfo const& info)
{
    return Napi::Number::New(info.Env(), tile_->y());
}

Napi::Value VectorTile::get_tile_z(Napi::CallbackInfo const& info)
{
    return Napi::Number::New(info.Env(), tile_->z());
}

Napi::Value VectorTile::get_tile_size(Napi::CallbackInfo const& info)
{
    return Napi::Number::New(info.Env(), tile_->tile_size());
}

Napi::Value VectorTile::get_buffer_size(Napi::CallbackInfo const& info)
{
    return Napi::Number::New(info.Env(), tile_->buffer_size());
}

void VectorTile::set_tile_x(Napi::CallbackInfo const& info, const Napi::Value& value)
{
    Napi::Env env = info.Env();
    if (!value.IsNumber())
    {
        Napi::Error::New(env, "Must provide a number").ThrowAsJavaScriptException();
    }
    else
    {
        int val = value.As<Napi::Number>().Int32Value();
        if (val < 0)
        {
            Napi::Error::New(env, "tile x coordinate must be greater then or equal to zero").ThrowAsJavaScriptException();
            return;
        }
        tile_->x(val);
    }
}

void VectorTile::set_tile_y(Napi::CallbackInfo const& info, const Napi::Value& value)
{
    Napi::Env env = info.Env();
    if (!value.IsNumber())
    {
        Napi::Error::New(env, "Must provide a number").ThrowAsJavaScriptException();

    }
    else
    {
        int val = value.As<Napi::Number>().Int32Value();
        if (val < 0)
        {
            Napi::Error::New(env, "tile y coordinate must be greater then or equal to zero").ThrowAsJavaScriptException();
            return;
        }
        tile_->y(val);
    }
}

void VectorTile::set_tile_z(Napi::CallbackInfo const& info, const Napi::Value& value)
{
    Napi::Env env = info.Env();
    if (!value.IsNumber())
    {
        Napi::Error::New(env, "Must provide a number").ThrowAsJavaScriptException();

    }
    else
    {
        int val = value.As<Napi::Number>().Int32Value();
        if (val < 0)
        {
            Napi::Error::New(env, "tile z coordinate must be greater then or equal to zero").ThrowAsJavaScriptException();
            return;
        }
        tile_->z(val);
    }
}

void VectorTile::set_tile_size(Napi::CallbackInfo const& info, const Napi::Value& value)
{
    Napi::Env env = info.Env();
    if (!value.IsNumber())
    {
        Napi::Error::New(env, "Must provide a number").ThrowAsJavaScriptException();

    }
    else
    {
        int val = value.As<Napi::Number>().Int32Value();
        if (val <= 0)
        {
            Napi::Error::New(env, "tile size must be greater then zero").ThrowAsJavaScriptException();
            return;
        }
        tile_->tile_size(val);
    }
}

void VectorTile::set_buffer_size(Napi::CallbackInfo const& info, const Napi::Value& value)
{
    Napi::Env env = info.Env();
    if (!value.IsNumber())
    {
        Napi::Error::New(env, "Must provide a number").ThrowAsJavaScriptException();

    }
    else
    {
        int val = value.As<Napi::Number>().Int32Value();
        if (static_cast<int>(tile_->tile_size()) + (2 * val) <= 0)
        {
            Napi::Error::New(env, "too large of a negative buffer for tilesize").ThrowAsJavaScriptException();
            return;
        }
        tile_->buffer_size(val);
    }
}
