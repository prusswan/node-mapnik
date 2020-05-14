#pragma once


namespace detail {

struct visitor_get_pixel
{
    visitor_get_pixel(Napi::Env env, int x, int y)
        : env_(env), x_(x), y_(y) {}

    Napi::Value operator() (mapnik::image_null const&)
    {
        // This should never be reached because the width and height of 0 for a null
        // image will prevent the visitor from being called.
        return env_.Undefined();
    }

    template <typename T>
    Napi::Value operator() (T const& data)
    {
        using image_type = T;
        using pixel_type = typename image_type::pixel_type;
        Napi::EscapableHandleScope scope(env_);
        pixel_type val = mapnik::get_pixel<pixel_type>(data, x_, y_);
        return scope.Escape(Napi::Number::New(env_, val));
    }

  private:
    Napi::Env env_;
    int x_;
    int y_;

};

struct visitor_set_pixel
{
    visitor_set_pixel(Napi::Number const& num, int x, int y)
        : num_(num), x_(x), y_(y) {}

    void operator() (mapnik::image_null &) const
    {
        // no-op
    }
    template <typename T>
    void operator() (T & image) const
    {
        mapnik::set_pixel(image, x_, y_, num_.DoubleValue());
    }
  private:
    Napi::Number const& num_;
    int x_;
    int y_;

};
} // ns detail
