/* $Id: rmutil.c,v 1.8 2003/08/26 13:14:59 rmagick Exp $ */
/*============================================================================\
|                Copyright (C) 2003 by Timothy P. Hunter
| Name:     rmutil.c
| Author:   Tim Hunter
| Purpose:  Utility functions for RMagick
\============================================================================*/

#include "rmagick.h"

static const char *Compliance_Const_Name(ComplianceType);
static const char *Style_Const_Name(StyleType);
static const char *Stretch_Const_Name(StretchType);
static void Color_Name_to_PixelPacket(PixelPacket *, VALUE);


/*
    Static:     strfcmp
    Purpose:    compare strings, ignoring case
    Returns:    same as strcmp
*/
static int
strfcmp(const char *s1, const char *s2)
{
    while (*s1 && *s2 && tolower(*s1) == tolower(*s2))
    {
        s1++;
        s2++;
    }

    return *s2-*s1;
}

/*
    Extern:     magick_malloc, magick_free, magick_realloc
    Purpose:    ****Magick versions of standard memory routines.
    Notes:      use when managing memory that ****Magick may have
                allocated or may free.
                If malloc fails, it raises an exception
*/
void *magick_malloc(const size_t size)
{
    void *ptr;
#if defined(HAVE_ACQUIREMAGICKMEMORY)
    ptr = AcquireMagickMemory(size);
#else
    ptr = AcquireMemory(size);
#endif
    if (!ptr)
    {
        rb_raise(rb_eNoMemError, "not enough memory to continue");
    }

    return ptr;
}

void magick_free(void *ptr)
{
#if defined(HAVE_ACQUIREMAGICKMEMORY)
    RelinquishMagickMemory(ptr);
#else
    void *v = ptr;
    LiberateMemory(&v);
#endif
}

void *magick_realloc(void *ptr, const size_t size)
{
    void *v;
#if defined(HAVE_ACQUIREMAGICKMEMORY)
    v = ResizeMagickMemory(ptr, size);
#else
    v = ptr;
    ReacquireMemory(&v, size);
#endif
    if (!v)
    {
        rb_raise(rb_eNoMemError, "not enough memory to continue");
    }
    return v;
}

/*
    Extern:     magick_clone_string
    Purpose:    make a copy of a string in malloc'd memory
    Notes:      Any existing string pointed to by *new_str is freed.
                If malloc fails, raises an exception
*/
void magick_clone_string(char **new_str, const char *str)
{
    unsigned int okay;

    okay = CloneString(new_str, str);
    if (!okay)
    {
        rb_raise(rb_eNoMemError, "not enough memory to continue");
    }
}

/*
    Extern:     rm_string_value_ptr(VALUE*)
    Purpose:    emulate Ruby 1.8's rb_string_value_ptr
    Notes:      This is essentially 1.8's rb_string_value_ptr
                with a few minor changes to make it work in 1.6.
                Always called via STRING_PTR
*/
#if RUBY_VERSION < 0x180
char *
rm_string_value_ptr(volatile VALUE *ptr)
{
    VALUE s = *ptr;

    // If VALUE is not a string, call to_str on it
    if (TYPE(s) != T_STRING)
    {
       s = rb_str_to_str(s);
       *ptr = s;
    }
    // If ptr == NULL, allocate a 1 char array
    if (!RSTRING(s)->ptr)
    {
        RSTRING(s)->ptr = ALLOC_N(char, 1);
        (RSTRING(s)->ptr)[0] = 0;
        RSTRING(s)->orig = 0;
    }
    return RSTRING(s)->ptr;
}
#endif

/*
    Extern:     rm_string_value_ptr_len
    Purpose:    safe replacement for rb_str2cstr
    Returns:    stores string length in 2nd arg, returns ptr to C string
    Notes:      Uses rb/rm_string_value_ptr to ensure correct String 
                argument. 
                Always called via STRING_PTR_LEN
*/
char *rm_string_value_ptr_len(volatile VALUE *ptr, Strlen_t *len)
{
    VALUE v = *ptr;
    char *str;

    str = STRING_PTR(v);
    *ptr = v;
    *len = RSTRING(v)->len;
    return str;
}

/*
    Extern:     ImageList_cur_image
    Purpose:    Sends the "cur_image" method to the object. If 'img'
                is an ImageList, then cur_image is self[@scene].
                If 'img' is an image, then cur_image is simply
                'self'.
    Returns:    the return value from "cur_image"
*/
VALUE
ImageList_cur_image(VALUE img)
{
    return rb_funcall(img, cur_image_ID, 0);
}

/*
    Method:     Magick::PrimaryInfo#to_s
    Purpose:    Create a string representation of a Magick::PrimaryInfo
*/
VALUE
PrimaryInfo_to_s(VALUE self)
{
    PrimaryInfo pi;
    char buff[100];

    Struct_to_PrimaryInfo(&pi, self);
    sprintf(buff, "x=%g, y=%g, z=%g", pi.x, pi.y, pi.z);
    return rb_str_new2(buff);
}

/*
    Method:     Magick::Chromaticity#to_s
    Purpose:    Create a string representation of a Magick::Chromaticity
*/
VALUE
ChromaticityInfo_to_s(VALUE self)
{
    ChromaticityInfo ci;
    char buff[200];

    Struct_to_ChromaticityInfo(&ci, self);
    sprintf(buff, "red_primary=(x=%g,y=%g) "
                  "green_primary=(x=%g,y=%g) "
                  "blue_primary=(x=%g,y=%g) "
                  "white_point=(x=%g,y=%g) ",
                  ci.red_primary.x, ci.red_primary.y,
                  ci.green_primary.x, ci.green_primary.y,
                  ci.blue_primary.x, ci.blue_primary.y,
                  ci.white_point.x, ci.white_point.y);
    return rb_str_new2(buff);
}

/*
    Method:     Magick::Pixel#to_s
    Purpose:    Create a string representation of a Magick::Pixel
*/
VALUE
Pixel_to_s(VALUE self)
{
    PixelPacket pp;
    char buff[100];

    Struct_to_PixelPacket(&pp, self);
    sprintf(buff, "red=%d, green=%d, blue=%d, opacity=%d"
          , pp.red, pp.green, pp.blue, pp.opacity);
    return rb_str_new2(buff);
}

/*
    Method:     Magick::Pixel.from_color(string)
    Purpose:    Construct an Magick::Pixel corresponding to the given color name.
    Notes:      the "inverse" is Image *#to_color, b/c the conversion of a pixel
                to a color name requires both a color depth and if the opacity
                value has meaning (i.e. whether image->matte == True or not).

                Also see Magick::Pixel#to_color, below.
*/
VALUE
Pixel_from_color(VALUE class, VALUE name)
{
    PixelPacket pp;
    ExceptionInfo exception;
    boolean okay;

    GetExceptionInfo(&exception);
    okay = QueryColorDatabase(STRING_PTR(name), &pp, &exception);
    HANDLE_ERROR
    if (!okay)
    {
        rb_raise(rb_eArgError, "invalid color name: %s", STRING_PTR(name));
    }

    return PixelPacket_to_Struct(&pp);
}

/*
    Method:     Magick::Pixel#to_color(compliance=Magick::???Compliance,
                                      matte=False
                                      depth=QuantumDepth)
    Purpose:    return the color name corresponding to the pixel values
    Notes:      the conversion respects the value of the 'opacity' field
                in the Pixel.
*/
VALUE
Pixel_to_color(int argc, VALUE *argv, VALUE self)
{
    Info *info;
    Image *image;
    PixelPacket pp;
    char name[MaxTextExtent];
    ExceptionInfo exception;
    ComplianceType compliance = AllCompliance;
    unsigned int matte = False;
    unsigned int depth = QuantumDepth;

    switch (argc)
    {
        case 3:
            depth = NUM2UINT(argv[2]);

            // This test ensures that depth is either 8 or, if QuantumDepth is
            // 16, 16.
            if (depth != 8 && depth != QuantumDepth)
            {
                rb_raise(rb_eArgError, "invalid depth (%d)", depth);
            }
        case 2:
            matte = RTEST(argv[1]);
        case 1:
            compliance = Num_to_ComplianceType(argv[0]);
        case 0:
            break;
        default:
            rb_raise(rb_eArgError, "wrong number of arguments (%d for 0 to 2)", argc);
    }

    Struct_to_PixelPacket(&pp, self);

    info = CloneImageInfo(NULL);
    image = AllocateImage(info);
    image->depth = depth;
    image->matte = matte;
    DestroyImageInfo(info);
    GetExceptionInfo(&exception);
    (void) QueryColorname(image, &pp, compliance, name, &exception);
    DestroyImage(image);
    HANDLE_ERROR

    // Always return a string, even if it's ""
    return rb_str_new2(name);
}

/*
    Method:     Pixel#to_HSL
    Purpose:    Converts an RGB pixel to the array
                [hue, saturation, luminosity].
*/
VALUE
Pixel_to_HSL(VALUE self)
{
    PixelPacket rgb;
    double hue, saturation, luminosity;
    VALUE hsl;

    Struct_to_PixelPacket(&rgb, self);
    TransformHSL(rgb.red, rgb.green, rgb.blue,
                 &hue, &saturation, &luminosity);

    hsl = rb_ary_new3(3, rb_float_new(hue), rb_float_new(saturation),
                      rb_float_new(luminosity));

    return hsl;
}

/*
    Method:     Pixel#from_HSL
    Purpose:    Constructs an RGB pixel from the array
                [hue, saturation, luminosity].
*/
VALUE
Pixel_from_HSL(VALUE self, VALUE hsl)
{
    PixelPacket rgb = {0};
    double hue, saturation, luminosity;

    Check_Type(hsl, T_ARRAY);

    hue        = NUM2DBL(rb_ary_entry(hsl, 0));
    saturation = NUM2DBL(rb_ary_entry(hsl, 1));
    luminosity = NUM2DBL(rb_ary_entry(hsl, 2));

    HSLTransform(hue, saturation, luminosity,
                 &rgb.red, &rgb.green, &rgb.blue);
    return PixelPacket_to_Struct(&rgb);
}

/*
    Method:     Magick::Rectangle#to_s
    Purpose:    Create a string representation of a Magick::Rectangle
*/
VALUE
RectangleInfo_to_s(VALUE self)
{
    RectangleInfo rect;
    char buff[100];

    Struct_to_RectangleInfo(&rect, self);
    sprintf(buff, "width=%lu, height=%lu, x=%ld, y=%ld"
          , rect.width, rect.height, rect.x, rect.y);
    return rb_str_new2(buff);
}

/*
    Method:     Magick::SegmentInfo#to_s
    Purpose:    Create a string representation of a Magick::Segment
*/
VALUE
SegmentInfo_to_s(VALUE self)
{
    SegmentInfo segment;
    char buff[100];

    Struct_to_SegmentInfo(&segment, self);
    sprintf(buff, "x1=%g, y1=%g, x2=%g, y2=%g"
          , segment.x1, segment.y1, segment.x2, segment.y2);
    return rb_str_new2(buff);
}

/*
    Extern:     PixelPacket_to_Color_Name
    Purpose:    Map the color intensity to a named color
    Returns:    the named color as a String
    Notes:      See below for the equivalent function that accepts an Info
                structure instead of an Image.
*/
VALUE
PixelPacket_to_Color_Name(Image *image, PixelPacket *color)
{
    char name[MaxTextExtent];
    ExceptionInfo exception;

    GetExceptionInfo(&exception);

    (void) QueryColorname(image, color, X11Compliance, name, &exception);
    HANDLE_IMG_ERROR(image)

    return rb_str_new2(name);
}

/*
    Extern:     PixelPacket_to_Color_Name_Info
    Purpose:    Map the color intensity to a named color
    Returns:    the named color as a String
    Notes:      Accepts an Info structure instead of an Image (see above).
                Simply create an Image from the Info, call QueryColorname,
                and then destroy the Image.
                If the Info structure is NULL, creates a new one.

                Note that the default depth is always used, and the matte
                value is set to False, which means "don't use the alpha channel".
*/
VALUE 
PixelPacket_to_Color_Name_Info(Info *info, PixelPacket *color)
{
    Image *image;
    Info *my_info;
    VALUE color_name;

    my_info = info ? info : CloneImageInfo(NULL);

    image = AllocateImage(info);
    image->matte = False;
    color_name = PixelPacket_to_Color_Name(image, color);
    DestroyImage(image);
    if (!info)
    {
        DestroyImageInfo(my_info);
    }

    return color_name;
}

/*
    Static:     Color_Name_to_PixelPacket
    Purpose:    Convert a color name to a PixelPacket
    Raises:     ArgumentError
*/
static void
Color_Name_to_PixelPacket(PixelPacket *color, VALUE name_arg)
{
    boolean okay;
    char *name;
    ExceptionInfo exception;

    GetExceptionInfo(&exception);
    name = STRING_PTR(name_arg);
    okay = QueryColorDatabase(name, color, &exception);
    DestroyExceptionInfo(&exception);
    if (!okay)
    {
        rb_raise(rb_eArgError, "invalid color name %s", name);
    }
}

/*
    Extern:     AffineMatrix_to_Struct
    Purpose:    Create a Magick::AffineMatrix object from an AffineMatrix structure.
*/
VALUE
AffineMatrix_to_Struct(AffineMatrix *am)
{
    return rb_funcall(Class_AffineMatrix, new_ID, 6
                    , rb_float_new(am->sx), rb_float_new(am->rx), rb_float_new(am->ry)
                    , rb_float_new(am->sy), rb_float_new(am->tx), rb_float_new(am->ty));
}

/*
    Extern:     Struct_to_AffineMatrix
    Purpose:    Convert a Magick::AffineMatrix object to a AffineMatrix structure.
    Notes:      If not initialized, the defaults are [sx,rx,ry,sy,tx,ty] = [1,0,0,1,0,0]
*/
void
Struct_to_AffineMatrix(AffineMatrix *am, VALUE st)
{
    VALUE values, v;

    if (CLASS_OF(st) != Class_AffineMatrix)
    {
        rb_raise(rb_eTypeError, "type mismatch: %s given",
                 rb_class2name(CLASS_OF(st)));
    }
    values = rb_funcall(st, values_ID, 0);
    v = rb_ary_entry(values, 0);
    am->sx = v == Qnil ? 1.0 : NUM2DBL(v);
    v = rb_ary_entry(values, 1);
    am->rx = v == Qnil ? 0.0 : NUM2DBL(v);
    v = rb_ary_entry(values, 2);
    am->ry = v == Qnil ? 0.0 : NUM2DBL(v);
    v = rb_ary_entry(values, 3);
    am->sy = v == Qnil ? 1.0 : NUM2DBL(v);
    v = rb_ary_entry(values, 4);
    am->tx = v == Qnil ? 0.0 : NUM2DBL(v);
    v = rb_ary_entry(values, 4);
    am->ty = v == Qnil ? 0.0 : NUM2DBL(v);
}

/*
    External:   ColorInfo_to_Struct
    Purpose:    Convert a ColorInfo structure to a Magick::Color
*/
VALUE
ColorInfo_to_Struct(const ColorInfo *ci)
{
    VALUE name;
    VALUE compliance;
    VALUE color;

    name       = rb_str_new2(ci->name);
    compliance = INT2FIX(ci->compliance);
    color      = PixelPacket_to_Struct((PixelPacket *)(&(ci->color)));

    return rb_funcall(Class_Color, new_ID, 3
                    , name, compliance, color);
}

/*
    External:   Struct_to_ColorInfo
    Purpose:    Convert a Magick::Color to a ColorInfo structure
*/
void
Struct_to_ColorInfo(ColorInfo *ci, VALUE st)
{
    VALUE members, m;

    if (CLASS_OF(st) != Class_Color)
    {
        rb_raise(rb_eTypeError, "type mismatch: %s given",
                 rb_class2name(CLASS_OF(st)));
    }

    memset(ci, '\0', sizeof(ColorInfo));

    members = rb_funcall(st, values_ID, 0);
                    
    m = rb_ary_entry(members, 0);
    if (m != Qnil)
    {
        CloneString((char **)&(ci->name), STRING_PTR(m));
    }
    m = rb_ary_entry(members, 1);
    if (m != Qnil)
    {
        ci->compliance = FIX2INT(m);
    }
    m = rb_ary_entry(members, 2);
    if (m != Qnil)
    {
        Struct_to_PixelPacket(&(ci->color), m);
    }
}

/*
    Static:     destroy_ColorInfo
    Purpose:    free the storage allocated by Struct_to_ColorInfo, above.
*/
static void
destroy_ColorInfo(ColorInfo *ci)
{
    magick_free((void*)ci->name);
    ci->name = NULL;
}

/*
    Method:     Color#to_s
    Purpose:    Return a string representation of a Magick::Color object
*/
VALUE
Color_to_s(VALUE self)
{
    ColorInfo ci;
    char buff[1024];

    Struct_to_ColorInfo(&ci, self);
    sprintf(buff, "name=%s, compliance=%s, "
                  "color.red=%d, color.green=%d, color.blue=%d, color.opacity=%d ",
                  ci.name,
                  Compliance_Const_Name(ci.compliance),
                  ci.color.red, ci.color.green, ci.color.blue, ci.color.opacity);

    destroy_ColorInfo(&ci);
    return rb_str_new2(buff);
}

/*
    Extern:     PixelPacket_to_Struct
    Purpose:    Create a Magick::Pixel object from a PixelPacket structure.
*/
VALUE
PixelPacket_to_Struct(PixelPacket *pp)
{
    return rb_funcall(Class_Pixel, new_ID, 4
                    , INT2FIX(pp->red), INT2FIX(pp->green)
                    , INT2FIX(pp->blue), INT2FIX(pp->opacity));
}

/*
    Extern:     Struct_to_PixelPacket
    Purpose:    Convert a Magick::Pixel object to a PixelPacket structure.
    Notes:      The Pixel object could have uninitialized values, default to 0.
*/
void
Struct_to_PixelPacket(PixelPacket *pp, VALUE st)
{
    VALUE values;
    VALUE c;

    if (CLASS_OF(st) != Class_Pixel)
    {
        rb_raise(rb_eTypeError, "type mismatch: %s given",
                 rb_class2name(CLASS_OF(st)));
    }

    values = rb_funcall(st, values_ID, 0);
    c = rb_ary_entry(values, 0);
    pp->red = c != Qnil ? NUM2INT(c) : 0;
    c = rb_ary_entry(values, 1);
    pp->green = c != Qnil ? NUM2INT(c) : 0;
    c = rb_ary_entry(values, 2);
    pp->blue = c != Qnil ? NUM2INT(c) : 0;
    c = rb_ary_entry(values, 3);
    pp->opacity = c != Qnil ? NUM2INT(c) : 0;
}

/*
    Extern:     Color_to_PixelPacket
    Purpose:    Convert either a String color name or
                a Magick::Pixel to a PixelPacket
*/
void
Color_to_PixelPacket(PixelPacket *pp, VALUE color)
{
    // Allow color name or Pixel
    if (TYPE(color) == T_STRING)
    {
        Color_Name_to_PixelPacket(pp, color);
    }
    else if (CLASS_OF(color) == Class_Pixel)
    {
        Struct_to_PixelPacket(pp, color);
    }
    else
    {
        rb_raise(rb_eTypeError, "color argument must be String or Pixel (%s given)",
                rb_class2name(CLASS_OF(color)));
    }

}

/*
    Extern:     PrimaryInfo_to_Struct(pp)
    Purpose:    Create a Magick::PrimaryInfo object from a PrimaryInfo structure.
*/
VALUE
PrimaryInfo_to_Struct(PrimaryInfo *p)
{
    return rb_funcall(Class_Primary, new_ID, 3
                    , INT2FIX(p->x), INT2FIX(p->y), INT2FIX(p->z));
}

/*
    Extern:     Struct_to_PrimaryInfo
    Purpose:    Convert a Magick::PrimaryInfo object to a PrimaryInfo structure
*/
void
Struct_to_PrimaryInfo(PrimaryInfo *pi, VALUE sp)
{
    VALUE members, m;

    if (CLASS_OF(sp) != Class_Primary)
    {
        rb_raise(rb_eTypeError, "type mismatch: %s given",
                 rb_class2name(CLASS_OF(sp)));
    }
    members = rb_funcall(sp, values_ID, 0);
    m = rb_ary_entry(members, 0);
    pi->x = m == Qnil ? 0 : FIX2INT(m);
    m = rb_ary_entry(members, 1);
    pi->y = m == Qnil ? 0 : FIX2INT(m);
    m = rb_ary_entry(members, 2);
    pi->z = m == Qnil ? 0 : FIX2INT(m);
}

/*
    Extern:     PointInfo_to_Struct(pp)
    Purpose:    Create a Magick::Point object from a PointInfo structure.
*/
VALUE
PointInfo_to_Struct(PointInfo *p)
{
    return rb_funcall(Class_Point, new_ID, 2
                    , INT2FIX(p->x), INT2FIX(p->y));
}

/*
    Extern:     Struct_to_PointInfo
    Purpose:    Convert a Magick::Point object to a PointInfo structure
*/
void
Struct_to_PointInfo(PointInfo *pi, VALUE sp)
{
    VALUE members, m;

    if (CLASS_OF(sp) != Class_Point)
    {
        rb_raise(rb_eTypeError, "type mismatch: %s given",
                 rb_class2name(CLASS_OF(sp)));
    }
    members = rb_funcall(sp, values_ID, 0);
    m = rb_ary_entry(members, 0);
    pi->x = m == Qnil ? 0 : FIX2INT(m);
    m = rb_ary_entry(members, 1);
    pi->y = m == Qnil ? 0 : FIX2INT(m);
}

/*
    Extern:     ChromaticityInfo_to_Struct(pp)
    Purpose:    Create a Magick::ChromaticityInfo object from a
                ChromaticityInfo structure.
*/
VALUE
ChromaticityInfo_to_Struct(ChromaticityInfo *ci)
{
    VALUE red_primary;
    VALUE green_primary;
    VALUE blue_primary;
    VALUE white_point;

    red_primary   = PrimaryInfo_to_Struct(&ci->red_primary);
    green_primary = PrimaryInfo_to_Struct(&ci->green_primary);
    blue_primary  = PrimaryInfo_to_Struct(&ci->blue_primary);
    white_point = PrimaryInfo_to_Struct(&ci->white_point);

    return rb_funcall(Class_Chromaticity, new_ID, 4
                    , red_primary, green_primary, blue_primary, white_point);
}

/*
    Extern:     Struct_to_ChromaticityInfo
    Purpose:    Extract the elements from a Magick::ChromaticityInfo
                and store in a ChromaticityInfo structure.
*/
void
Struct_to_ChromaticityInfo(ChromaticityInfo *ci, VALUE chrom)
{
    VALUE chrom_members;
    VALUE red_primary, green_primary, blue_primary, white_point;
    VALUE entry_members, x, y;
    ID values_id;

    if (CLASS_OF(chrom) != Class_Chromaticity)
    {
        rb_raise(rb_eTypeError, "type mismatch: %s given",
                 rb_class2name(CLASS_OF(chrom)));
    }
    values_id = values_ID;

    // Get the struct members in an array
    chrom_members = rb_funcall(chrom, values_id, 0);
    red_primary   = rb_ary_entry(chrom_members, 0);
    green_primary = rb_ary_entry(chrom_members, 1);
    blue_primary  = rb_ary_entry(chrom_members, 2);
    white_point = rb_ary_entry(chrom_members, 3);

    // Get the red_primary PrimaryInfo members in an array
    entry_members = rb_funcall(red_primary, values_id, 0);
    x = rb_ary_entry(entry_members, 0);         // red_primary.x
    ci->red_primary.x = x == Qnil ? 0.0 : NUM2DBL(x);
    y = rb_ary_entry(entry_members, 1);         // red_primary.y
    ci->red_primary.y = y == Qnil ? 0.0 : NUM2DBL(y);
    ci->red_primary.z = 0.0;

    // Get the green_primary PrimaryInfo members in an array
    entry_members = rb_funcall(green_primary, values_id, 0);
    x = rb_ary_entry(entry_members, 0);         // green_primary.x
    ci->green_primary.x = x == Qnil ? 0.0 : NUM2DBL(x);
    y = rb_ary_entry(entry_members, 1);         // green_primary.y
    ci->green_primary.y = y == Qnil ? 0.0 : NUM2DBL(y);
    ci->green_primary.z = 0.0;

    // Get the blue_primary PrimaryInfo members in an array
    entry_members = rb_funcall(blue_primary, values_id, 0);
    x = rb_ary_entry(entry_members, 0);         // blue_primary.x
    ci->blue_primary.x = x == Qnil ? 0.0 : NUM2DBL(x);
    y = rb_ary_entry(entry_members, 1);         // blue_primary.y
    ci->blue_primary.y = y == Qnil ? 0.0 : NUM2DBL(y);
    ci->blue_primary.z = 0.0;

    // Get the white_point PrimaryInfo members in an array
    entry_members = rb_funcall(white_point, values_id, 0);
    x = rb_ary_entry(entry_members, 0);         // white_point.x
    ci->white_point.x = x == Qnil ? 0.0 : NUM2DBL(x);
    y = rb_ary_entry(entry_members, 1);         // white_point.y
    ci->white_point.y = y == Qnil ? 0.0 : NUM2DBL(y);
    ci->white_point.z = 0.0;
}

/*
    External:   RectangleInfo_to_Struct
    Purpose:    Convert a RectangleInfo structure to a Magick::Rectangle
*/
VALUE
RectangleInfo_to_Struct(RectangleInfo *rect)
{
    VALUE width;
    VALUE height;
    VALUE x, y;

    width  = UINT2NUM(rect->width);
    height = UINT2NUM(rect->height);
    x      = INT2NUM(rect->x);
    y      = INT2NUM(rect->y);
    return rb_funcall(Class_Rectangle, new_ID, 4
                    , width, height, x, y);
}

/*
    External:   Struct_to_RectangleInfo
    Purpose:    Convert a Magick::Rectangle to a RectangleInfo structure.
*/
void
Struct_to_RectangleInfo(RectangleInfo *rect, VALUE sr)
{
    VALUE members, m;

    if (CLASS_OF(sr) != Class_Rectangle)
    {
        rb_raise(rb_eTypeError, "type mismatch: %s given",
                 rb_class2name(CLASS_OF(sr)));
    }
    members = rb_funcall(sr, values_ID, 0);
    m = rb_ary_entry(members, 0);
    rect->width  = m == Qnil ? 0 : NUM2ULONG(m);
    m = rb_ary_entry(members, 1);
    rect->height = m == Qnil ? 0 : NUM2ULONG(m);
    m = rb_ary_entry(members, 2);
    rect->x      = m == Qnil ? 0 : NUM2LONG (m);
    m = rb_ary_entry(members, 3);
    rect->y      = m == Qnil ? 0 : NUM2LONG (m);
}

/*
    External:   SegmentInfo_to_Struct
    Purpose:    Convert a SegmentInfo structure to a Magick::Segment
*/
VALUE
SegmentInfo_to_Struct(SegmentInfo *segment)
{
    VALUE x1, y1, x2, y2;

    x1 = rb_float_new(segment->x1);
    y1 = rb_float_new(segment->y1);
    x2 = rb_float_new(segment->x2);
    y2 = rb_float_new(segment->y2);
    return rb_funcall(Class_Segment, new_ID, 4, x1, y1, x2, y2);
}

/*
    External:   Struct_to_SegmentInfo
    Purpose:    Convert a Magick::Segment to a SegmentInfo structure.
*/
void
Struct_to_SegmentInfo(SegmentInfo *segment, VALUE s)
{
    VALUE members, m;

    if (CLASS_OF(s) != Class_Segment)
    {
        rb_raise(rb_eTypeError, "type mismatch: %s given",
                 rb_class2name(CLASS_OF(s)));
    }

    members = rb_funcall(s, values_ID, 0);
    m = rb_ary_entry(members, 0);
    segment->x1 = m == Qnil ? 0.0 : NUM2DBL(m);
    m = rb_ary_entry(members, 1);
    segment->y1 = m == Qnil ? 0.0 : NUM2DBL(m);
    m = rb_ary_entry(members, 2);
    segment->x2 = m == Qnil ? 0.0 : NUM2DBL(m);
    m = rb_ary_entry(members, 3);
    segment->y2 = m == Qnil ? 0.0 : NUM2DBL(m);
}

/*
    External:   TypeInfo_to_Struct
    Purpose:    Convert a TypeInfo structure to a Magick::Font
*/
VALUE
TypeInfo_to_Struct(TypeInfo *ti)
{
    VALUE name, description, family;
    VALUE style, stretch, weight;
    VALUE encoding, foundry, format;

    name        = rb_str_new2(ti->name);
    description = rb_str_new2(ti->description);
    family      = rb_str_new2(ti->family);
    style       = INT2FIX(ti->style);
    stretch     = INT2FIX(ti->stretch);
    weight      = INT2NUM(ti->weight);
    encoding    = ti->encoding ? rb_str_new2(ti->encoding) : Qnil;
    foundry     = ti->foundry  ? rb_str_new2(ti->foundry)  : Qnil;
    format      = ti->format   ? rb_str_new2(ti->format)   : Qnil;

    return rb_funcall(Class_Font, new_ID, 9
                    , name, description, family, style
                    , stretch, weight, encoding, foundry, format);
}

/*
    External:   Struct_to_TypeInfo
    Purpose:    Convert a Magick::Font to a TypeInfo structure
*/
void
Struct_to_TypeInfo(TypeInfo *ti, VALUE st)
{
    VALUE members, m;

    if (CLASS_OF(st) != Class_Font)
    {
        rb_raise(rb_eTypeError, "type mismatch: %s given",
                 rb_class2name(CLASS_OF(st)));
    }

    memset(ti, '\0', sizeof(TypeInfo));

    members = rb_funcall(st, values_ID, 0);
    m = rb_ary_entry(members, 0);
    if (m != Qnil)
    {
        CloneString((char **)&(ti->name), STRING_PTR(m));
    }
    m = rb_ary_entry(members, 1);
    if (m != Qnil)
    {
        CloneString((char **)&(ti->description), STRING_PTR(m));
    }
    m = rb_ary_entry(members, 2);
    if (m != Qnil)
    {
        CloneString((char **)&(ti->family), STRING_PTR(m));
    }
    m = rb_ary_entry(members, 3); ti->style   = m == Qnil ? 0 : FIX2INT(m);
    m = rb_ary_entry(members, 4); ti->stretch = m == Qnil ? 0 : FIX2INT(m);
    m = rb_ary_entry(members, 5); ti->weight  = m == Qnil ? 0 : FIX2INT(m);
    
    m = rb_ary_entry(members, 6);
    if (m != Qnil)
        CloneString((char **)&(ti->encoding), STRING_PTR(m));
    m = rb_ary_entry(members, 7);
    if (m != Qnil)
        CloneString((char **)&(ti->foundry), STRING_PTR(m));
    m = rb_ary_entry(members, 8);
    if (m != Qnil)
        CloneString((char **)&(ti->format), STRING_PTR(m));
}

/*
    Static:     destroy_TypeInfo
    Purpose:    free the storage allocated by Struct_to_TypeInfo, above.
*/
static void
destroy_TypeInfo(TypeInfo *ti)
{
    magick_free((void*)ti->name);
    ti->name = NULL;
    magick_free((void*)ti->description);
    ti->description = NULL;
    magick_free((void*)ti->family);
    ti->family = NULL;
    magick_free((void*)ti->encoding);
    ti->encoding = NULL;
    magick_free((void*)ti->foundry);
    ti->foundry = NULL;
    magick_free((void*)ti->format);
    ti->format = NULL;
}

/*
    External:   Font_to_s
    Purpose:    implement the Font#to_s method
*/
VALUE
Font_to_s(VALUE self)
{
    TypeInfo ti;
    char weight[20];
    char buff[1024];

    Struct_to_TypeInfo(&ti, self);

    switch (ti.weight)
    {
        case 400:
            strcpy(weight, "NormalWeight");
            break;
        case 700:
            strcpy(weight, "BoldWeight");
            break;
        default:
            sprintf(weight, "%ld", ti.weight);
            break;
    }

    sprintf(buff, "name=%s, description=%s, "
                  "family=%s, style=%s, stretch=%s, weight=%s, "
                  "encoding=%s, foundry=%s, format=%s",
                  ti.name,
                  ti.description,
                  ti.family,
                  Style_Const_Name(ti.style),
                  Stretch_Const_Name(ti.stretch),
                  weight,
                  ti.encoding ? ti.encoding : "",
                  ti.foundry ? ti.foundry : "",
                  ti.format ? ti.format : "");

    destroy_TypeInfo(&ti);
    return rb_str_new2(buff);

}

/*
    External:   TypeMetric_to_Struct
    Purpose:    Convert a TypeMetric structure to a Magick::TypeMetric
*/
VALUE
TypeMetric_to_Struct(TypeMetric *tm)
{
    VALUE pixels_per_em;
    VALUE ascent, descent;
    VALUE width, height, max_advance;
    VALUE bounds, underline_position, underline_thickness;

    pixels_per_em       = PointInfo_to_Struct(&tm->pixels_per_em);
    ascent              = rb_float_new(tm->ascent);
    descent             = rb_float_new(tm->descent);
    width               = rb_float_new(tm->width);
    height              = rb_float_new(tm->height);
    max_advance         = rb_float_new(tm->max_advance);
    bounds              = SegmentInfo_to_Struct(&tm->bounds);
    underline_position  = rb_float_new(tm->underline_position);
    underline_thickness = rb_float_new(tm->underline_position);

    return rb_funcall(Class_TypeMetric, new_ID, 9
                    , pixels_per_em, ascent, descent, width
                    , height, max_advance, bounds
                    , underline_position, underline_thickness);
}

/*
    External:   Struct_to_TypeMetric
    Purpose:    Convert a Magick::TypeMetric to a TypeMetric structure.
*/
void
Struct_to_TypeMetric(TypeMetric *tm, VALUE st)
{
    VALUE members, m;
    VALUE pixels_per_em;

    if (CLASS_OF(st) != Class_TypeMetric)
    {
        rb_raise(rb_eTypeError, "type mismatch: %s given",
                 rb_class2name(CLASS_OF(st)));
    }
    members = rb_funcall(st, values_ID, 0);
                    
    pixels_per_em   = rb_ary_entry(members, 0);
    Struct_to_PointInfo(&tm->pixels_per_em, pixels_per_em);

    m = rb_ary_entry(members, 1);
    tm->ascent      = m == Qnil ? 0.0 : NUM2DBL(m);
    m = rb_ary_entry(members, 2);
    tm->descent     = m == Qnil ? 0.0 : NUM2DBL(m);
    m = rb_ary_entry(members, 3);
    tm->width       = m == Qnil ? 0.0 : NUM2DBL(m);
    m = rb_ary_entry(members, 4);
    tm->height      = m == Qnil ? 0.0 : NUM2DBL(m);
    m = rb_ary_entry(members, 5);
    tm->max_advance = m == Qnil ? 0.0 : NUM2DBL(m);

    m = rb_ary_entry(members, 6);
    Struct_to_SegmentInfo(&tm->bounds, m);

    m = rb_ary_entry(members, 7);
    tm->underline_position  = m == Qnil ? 0.0 : NUM2DBL(m);
    m = rb_ary_entry(members, 8);
    tm->underline_thickness = m == Qnil ? 0.0 : NUM2DBL(m);
}

/*
    Method:     Magick::TypeMetric#to_s
    Purpose:    Create a string representation of a Magick::TypeMetric
*/
VALUE
TypeMetric_to_s(VALUE self)
{
    TypeMetric tm;
    char buff[200];

    Struct_to_TypeMetric(&tm, self);
    sprintf(buff, "pixels_per_em=(x=%g,y=%g) "
                  "ascent=%g descent=%g width=%g height=%g max_advance=%g "
                  "bounds.x1=%g bounds.y1=%g bounds.x2=%g bounds.y2=%g "
                  "underline_position=%g underline_thickness=%g",
                  tm.pixels_per_em.x, tm.pixels_per_em.y,
                  tm.ascent, tm.descent, tm.width, tm.height, tm.max_advance,
                  tm.bounds.x1, tm.bounds.y1, tm.bounds.x2, tm.bounds.y2,
                  tm.underline_position, tm.underline_thickness);
    return rb_str_new2(buff);
}

/*
    External:   Num_to_AlignType
    Purpose:    Convert a Numeric value to a AlignType value.
                Raise an exception if the value is not a AlignType.
*/
AlignType
Num_to_AlignType(VALUE type)
{
    static AlignType at[] = {
        UndefinedAlign,
        LeftAlign,
        CenterAlign,
        RightAlign
 };
#define ALIGN_N (sizeof(at)/sizeof(at[0]))
    int x;
    AlignType atype;

    atype = NUM2INT(type);
    for (x = 0; x < ALIGN_N; x++)
    {
        if (atype == at[x]) break;
    }

    if (x == ALIGN_N)
    {
        rb_raise(rb_eArgError, "invalid AlignType constant (%d)", atype);
    }

    return atype;
}

/*
    External:   Num_to_ChannelType
    Purpose:    Convert a Numeric value to a ChannelType value.
                Raise an exception if the value is not a ChannelType.
*/
ChannelType
Num_to_ChannelType(VALUE type)
{
    static ChannelType ct[] = {
        UndefinedChannel,
        RedChannel,
        CyanChannel,
        GreenChannel,
        MagentaChannel,
        BlueChannel,
        YellowChannel,
        OpacityChannel,
        BlackChannel,
        MatteChannel
 };
#define CHANNEL_N (sizeof(ct)/sizeof(ct[0]))
    int x;
    ChannelType ctype;

    ctype = NUM2INT(type);
    for (x = 0; x < CHANNEL_N; x++)
    {
        if (ctype == ct[x]) break;
    }

    if (x == CHANNEL_N)
    {
        rb_raise(rb_eArgError, "invalid ChannelType constant (%d)", ctype);
    }

    return ctype;
}

/*
    External:   Num_to_ClassType
    Purpose:    Convert a Numeric value to a ClassType value.
                Raise an exception if the value is not a ClassType.
*/
ClassType
Num_to_ClassType(VALUE type)
{
    static ClassType ct[] = {
        UndefinedClass,
        DirectClass,
        PseudoClass
 };
#define CHANNEL_N (sizeof(ct)/sizeof(ct[0]))
    int x;
    ClassType ctype;

    ctype = NUM2INT(type);
    for (x = 0; x < CHANNEL_N; x++)
    {
        if (ctype == ct[x]) break;
    }

    if (x == CHANNEL_N)
    {
        rb_raise(rb_eArgError, "invalid ClassType constant (%d)", ctype);
    }

    return ctype;
}

/*
    Static:     Compliance_Const_Name
    Purpose:    Return the string representation of a ComplianceType value
*/
static const char *
Compliance_Const_Name(ComplianceType c)
{
    switch (c)
    {
        case NoCompliance:
            return "NoCompliance";
        case SVGCompliance:
            return "SVGCompliance";
        case X11Compliance:
            return "X11Compliance";
        case XPMCompliance:
            return "XPMCompliance";
// AllCompliance is not defined in 5.4.9
        case AllCompliance:
            return "AllCompliance";
        default:
            return "unknown";
        }
}

/*
    External:   Num_to_ComplianceType
    Purpose:    Convert a Numeric value to a ComplianceType value.
                Raise an exception if the value is not a ComplianceType.
*/
ComplianceType
Num_to_ComplianceType(VALUE type)
{
    static ComplianceType ct[] = {
#if HAVE_NOCOMPLIANCE
        NoCompliance,
#endif
        SVGCompliance,
        X11Compliance,
        XPMCompliance,
        AllCompliance
 };
#define COMPLIANCE_N (sizeof(ct)/sizeof(ct[0]))
    int x;
    ComplianceType ctype;

    ctype = NUM2INT(type);
    for (x = 0; x < COMPLIANCE_N; x++)
    {
        if (ctype == ct[x]) break;
    }

    if (x == COMPLIANCE_N)
    {
        rb_raise(rb_eArgError, "invalid ComplianceType constant (%d)", ctype);
    }

    return ctype;
}

/*
    External:   Num_to_CompositeOperator
    Purpose:    Convert a Numeric value to a CompositeOperator value.
                Raise an exception if the value is not a CompositeOperator.
*/
CompositeOperator
Num_to_CompositeOperator(VALUE type)
{
    static CompositeOperator cm[] = {
        UndefinedCompositeOp,
        OverCompositeOp,
        InCompositeOp,
        OutCompositeOp,
        AtopCompositeOp,
        XorCompositeOp,
        PlusCompositeOp,
        MinusCompositeOp,
        AddCompositeOp,
        SubtractCompositeOp,
        DifferenceCompositeOp,
        MultiplyCompositeOp,
        BumpmapCompositeOp,
        CopyCompositeOp,
        CopyRedCompositeOp,
        CopyGreenCompositeOp,
        CopyBlueCompositeOp,
        CopyOpacityCompositeOp,
        ClearCompositeOp,
        DissolveCompositeOp,
        DisplaceCompositeOp,
        ModulateCompositeOp,
        ThresholdCompositeOp,
        NoCompositeOp,
        DarkenCompositeOp,
        LightenCompositeOp,
        HueCompositeOp,
        SaturateCompositeOp,
        ColorizeCompositeOp,
        LuminizeCompositeOp,
        ScreenCompositeOp,
        OverlayCompositeOp,
#if defined(HAVE_COPYCYANCOMPOSITEOP)
        CopyCyanCompositeOp,
        CopyMagentaCompositeOp,
        CopyYellowCompositeOp,
        CopyBlackCompositeOp,
#endif
    };
#define COMPOSITE_N (sizeof(cm)/sizeof(cm[0]))
    int x;
    CompositeOperator cop;

    cop = NUM2INT(type);
    for (x = 0; x < COMPOSITE_N; x++)
    {
        if (cop == cm[x]) break;
    }

    if (x == COMPOSITE_N)
    {
        rb_raise(rb_eArgError, "invalid CompositeOperator constant (%d)", cop);
    }

    return cop;
}

/*
    External:   Num_to_CompressionType
    Purpose:    Convert a Numeric value to a CompressionType value.
                Raise an exception if the value is not a CompressionType.
*/
CompressionType
Num_to_CompressionType(VALUE type)
{
    static CompressionType cm[] = {
        UndefinedCompression, NoCompression, BZipCompression, FaxCompression,
        Group4Compression, JPEGCompression, LosslessJPEGCompression,
        LZWCompression, RunlengthEncodedCompression, ZipCompression };
#define COMPRESSION_N (sizeof(cm)/sizeof(cm[0]))
    int x;
    CompressionType ctype;

    ctype = NUM2INT(type);
    for (x = 0; x < COMPRESSION_N; x++)
    {
        if (ctype == cm[x]) break;
    }

    if (x == COMPRESSION_N)
    {
        rb_raise(rb_eArgError, "invalid CompressionType constant (%d)", ctype);
    }

    return ctype;
}

/*
    External:   Num_to_DecorationType
    Purpose:    Convert a Numeric value to a DecorationType value.
                Raise an exception if the value is not a DecorationType.
*/
DecorationType
Num_to_DecorationType(VALUE type)
{
    static DecorationType dec[] = {
        NoDecoration,
        UnderlineDecoration,
        OverlineDecoration,
        LineThroughDecoration
    };
#define DECORATION_N (sizeof(dec)/sizeof(dec[0]))
    int x;
    DecorationType decoration;

    decoration = NUM2INT(type);
    for (x = 0; x < DECORATION_N; x++)
    {
        if (decoration == dec[x]) break;
    }

    if (x == DECORATION_N)
    {
        rb_raise(rb_eArgError, "invalid DecorationType constant (%d)", decoration);
    }
    return decoration;
}

#if HAVE_DISPOSETYPE
/*
    External:   Num_to_DisposeType
    Purpose:    Convert a Numeric value to a DisposeType value.
                Raise an exception if the value is not a DisposeType.
*/
DisposeType
Num_to_DisposeType(VALUE type)
{
    static DisposeType dt[] = {
        UndefinedDispose,
        NoneDispose,
        BackgroundDispose,
        PreviousDispose
    };
#define DISPOSE_N (sizeof(dt)/sizeof(dt[0]))
    int x;
    DisposeType dtype;

    dtype = NUM2INT(type);
    for (x = 0; x < DISPOSE_N; x++)
    {
        if (dtype == dt[x]) break;
    }

    if (x == DISPOSE_N)
    {
        rb_raise(rb_eArgError, "invalid DisposeType constant (%d)", dtype);
    }

    return dtype;
}
#endif

/*
    External:   Num_to_FilterType
    Purpose:    Convert a Numeric value to a FilterType value.
                Raise an exception if the value is not a FilterType.
*/
FilterTypes
Num_to_FilterType(VALUE type)
{
    static FilterTypes ft[] = {
        UndefinedFilter,
        PointFilter,
        BoxFilter,
        TriangleFilter,
        HermiteFilter,
        HanningFilter,
        HammingFilter,
        BlackmanFilter,
        GaussianFilter,
        QuadraticFilter,
        CubicFilter,
        CatromFilter,
        MitchellFilter,
        LanczosFilter,
        BesselFilter,
        SincFilter
    };
#define FILTER_N (sizeof(ft)/sizeof(ft[0]))
    int x;
    FilterTypes ftype;

    ftype = NUM2INT(type);
    for (x = 0; x < FILTER_N; x++)
    {
        if (ftype == ft[x]) break;
    }

    if (x == FILTER_N)
    {
        rb_raise(rb_eArgError, "invalid FilterType constant (%d)", ftype);
    }

    return ftype;
}

/*
    External:   Num_to_GravityType
    Purpose:    Convert a Numeric value to a GravityType value.
                Raise an exception if the value is not a GravityType.
*/
GravityType
Num_to_GravityType(VALUE type)
{
    static GravityType gtype[] = {
        ForgetGravity,
        NorthWestGravity,
        NorthGravity,
        NorthEastGravity,
        WestGravity,
        CenterGravity,
        EastGravity,
        SouthWestGravity,
        SouthGravity,
        SouthEastGravity,
        StaticGravity
    };
#define GRAVITYTYPE_N (sizeof(gtype)/sizeof(gtype[0]))
    int x;
    GravityType grav;

    grav = NUM2INT(type);
    for (x = 0; x < GRAVITYTYPE_N; x++)
    {
        if (grav == gtype[x]) break;
    }

    if (x == GRAVITYTYPE_N)
    {
        rb_raise(rb_eArgError, "invalid GravityType (%d)", grav);
    }
    return grav;
}

/*
    External:   Num_to_ImageType
    Purpose:    Convert a Numeric value to a ImageType value.
                Raise an exception if the value is not a ImageType.
*/
ImageType
Num_to_ImageType(VALUE type)
{
    static ImageType imgtype[] = {
        UndefinedType,
        BilevelType,
        GrayscaleType,
        GrayscaleMatteType,
        PaletteType,
        PaletteMatteType,
        TrueColorType,
        TrueColorMatteType,
        ColorSeparationType,
        ColorSeparationMatteType,
        OptimizeType
    };
#define IMAGETYPE_N (sizeof(imgtype)/sizeof(imgtype[0]))
    int x;
    ImageType it;

    it = NUM2INT(type);
    for (x = 0; x < IMAGETYPE_N; x++)
    {
        if (it == imgtype[x]) break;
    }

    if (x == IMAGETYPE_N)
    {
        rb_raise(rb_eArgError, "invalid ImageType constant (%d)", it);
    }

    return it;
}

/*
    External:   Num_to_InterlaceType
    Purpose:    Convert a Numeric value to a InterlaceType value.
                Raise an exception if the value is not a InterlaceType.
*/
InterlaceType
Num_to_InterlaceType(VALUE type)
{
    static InterlaceType it[] = {
        UndefinedInterlace,
        NoInterlace,
        LineInterlace,
        PlaneInterlace,
        PartitionInterlace
        };
#define INTERLACE_N (sizeof(it)/sizeof(it[0]))
    int x;
    InterlaceType itype;

    itype = NUM2INT(type);
    for (x = 0; x < INTERLACE_N; x++)
    {
        if (itype == it[x]) break;
    }

    if (x == INTERLACE_N)
    {
        rb_raise(rb_eArgError, "invalid InterlaceType constant (%d)", itype);
    }

    return itype;
}

/*
    External:   Num_to_ColorspaceType
    Purpose:    Convert a Numeric value to a ColorspaceType value.
                Raise an exception if the value is not a ColorspaceType.
*/
ColorspaceType
Num_to_ColorspaceType(VALUE type)
{
    static ColorspaceType cs[] = {
        UndefinedColorspace, RGBColorspace, GRAYColorspace, TransparentColorspace,
        OHTAColorspace, XYZColorspace, YCbCrColorspace, YCCColorspace, YIQColorspace,
        YPbPrColorspace, YUVColorspace, CMYKColorspace, sRGBColorspace,
#if defined(HAVE_HSLCOLORSPACE)
        HSLColorspace,
#endif
#if defined(HAVE_HWBCOLORSPACE)
        HWBColorspace,
#endif
        };
#define COLORSPACE_N (sizeof(cs)/sizeof(cs[0]))
    int x;
    ColorspaceType cstype;

    cstype = NUM2INT(type);
    for (x = 0; x < COLORSPACE_N; x++)
    {
        if (cstype == cs[x]) break;
    }

    if (x == COLORSPACE_N)
    {
        rb_raise(rb_eArgError, "invalid ColorspaceType constant (%d)", cstype);
    }

    return cstype;
}

/*
    External:   Num_to_NoiseType
    Purpose:    Convert a Numeric value to a NoiseType value.
                Raise an exception if the number is not a NoiseType value.
*/
NoiseType
Num_to_NoiseType(VALUE noise)
{
    static NoiseType types[] = {
        UniformNoise,
        GaussianNoise,
        MultiplicativeGaussianNoise,
        ImpulseNoise,
        LaplacianNoise,
        PoissonNoise
    };
#define NOISETYPE_N (sizeof(types)/sizeof(types[0]))
    int x;
    NoiseType n;

    n = NUM2INT(noise);
    for (x = 0; x < NOISETYPE_N; x++)
    {
        if (n == types[x]) break;
    }

    if (x == NOISETYPE_N)
    {
        rb_raise(rb_eArgError, "invalid NoiseType constant (%d)", n);
    }

    return n;
}

/*
    External:   Num_to_RenderingIntent
    Purpose:    Convert a Numeric value to a RenderingIntent value.
                Raise an exception if the value is not a RenderingIntent.
*/
RenderingIntent
Num_to_RenderingIntent(VALUE type)
{
    static RenderingIntent rs[] = {
        UndefinedIntent,
        SaturationIntent,
        PerceptualIntent,
        AbsoluteIntent,
        RelativeIntent
        };
#define RENDERING_N (sizeof(rs)/sizeof(rs[0]))
    int x;
    RenderingIntent rtype;

    rtype = NUM2INT(type);
    for (x = 0; x < RENDERING_N; x++)
    {
        if (rtype == rs[x]) break;
    }

    if (x == RENDERING_N)
    {
        rb_raise(rb_eArgError, "invalid RenderingIntent constant (%d)", rtype);
    }

    return rtype;
}

/*
    External:   Num_to_ResolutionType
    Purpose:    Convert a Numeric value to a ResolutionType value.
                Raise an exception if the value is not a ResolutionType.
*/
ResolutionType
Num_to_ResolutionType(VALUE type)
{
    static ResolutionType rs[] = {
        UndefinedResolution,
        PixelsPerInchResolution,
        PixelsPerCentimeterResolution
        };
#define RESOLUTION_N (sizeof(rs)/sizeof(rs[0]))
    int x;
    ResolutionType rtype;

    rtype = NUM2INT(type);
    for (x = 0; x < RESOLUTION_N; x++)
    {
        if (rtype == rs[x]) break;
    }

    if (x == RESOLUTION_N)
    {
        rb_raise(rb_eArgError, "invalid ResolutionType constant (%d)", rtype);
    }

    return rtype;
}

/*
    External:   Num_to_PaintMethod
    Purpose:    Convert a Numeric value to a PaintMethod value.
                Raise an exception if the number is not a PaintMethod value.
*/
PaintMethod
Num_to_PaintMethod(VALUE method)
{
    static PaintMethod meth[] = {
        PointMethod,
        ReplaceMethod,
        FloodfillMethod,
        FillToBorderMethod,
        ResetMethod
    };
#define PAINTMETHOD_N (sizeof(meth)/sizeof(meth[0]))
    int x;
    PaintMethod m;

    m = NUM2INT(method);
    for (x = 0; x < PAINTMETHOD_N; x++)
    {
        if (m == meth[x]) break;
    }

    if (x == PAINTMETHOD_N)
    {
        rb_raise(rb_eArgError, "invalid PaintMethod constant (%d)", m);
    }

    return m;
}

/*
    External:   Num_to_StretchType
    Purpose:    Convert a Numeric value to a StretchType value.
                Raise an exception if the number is not a StretchType value.
*/
StretchType
Num_to_StretchType(VALUE stretch)
{
    static StretchType types[] = {
        NormalStretch,
        UltraCondensedStretch,
        ExtraCondensedStretch,
        CondensedStretch,
        SemiCondensedStretch,
        SemiExpandedStretch,
        ExpandedStretch,
        ExtraExpandedStretch,
        UltraExpandedStretch,
        AnyStretch
    };
#define STRETCHTYPE_N (sizeof(types)/sizeof(types[0]))
    int x;
    StretchType n;

    n = NUM2INT(stretch);
    for (x = 0; x < STRETCHTYPE_N; x++)
    {
        if (n == types[x]) break;
    }

    if (x == STRETCHTYPE_N)
    {
        rb_raise(rb_eArgError, "invalid StretchType constant (%d)", n);
    }

    return n;
}

static const char *
Stretch_Const_Name(StretchType stretch)
{
    switch (stretch)
    {
        case NormalStretch:
            return "NormalStretch";
        case UltraCondensedStretch:
            return "UltraCondensedStretch";
        case ExtraCondensedStretch:
            return "ExtraCondensedStretch";
        case CondensedStretch:
            return "CondensedStretch";
        case SemiCondensedStretch:
            return "SemiCondensedStretch";
        case SemiExpandedStretch:
            return "SemiExpandedStretch";
        case ExpandedStretch:
            return "ExpandedStretch";
        case ExtraExpandedStretch:
            return "ExtraExpandedStretch";
        case UltraExpandedStretch:
            return "UltraExpandedStretch";
        case AnyStretch:
            return "AnyStretch";
        default:
            return "unknown";
    }
}

/*
    External:   Num_to_StyleType
    Purpose:    Convert a Numeric value to a StyleType value.
                Raise an exception if the number is not a StyleType value.
*/
StyleType
Num_to_StyleType(VALUE style)
{
    static StyleType types[] = {
        NormalStyle,
        ItalicStyle,
        ObliqueStyle,
        AnyStyle
    };
#define STYLETYPE_N (sizeof(types)/sizeof(types[0]))
    int x;
    StyleType n;

    n = NUM2INT(style);
    for (x = 0; x < STYLETYPE_N; x++)
    {
        if (n == types[x]) break;
    }

    if (x == STYLETYPE_N)
    {
        rb_raise(rb_eArgError, "invalid StyleType constant (%d)", n);
    }

    return n;
}

static const char *
Style_Const_Name(StyleType style)
{
    switch (style)
    {
        case NormalStyle:
            return "NormalStyle";
        case ItalicStyle:
            return "ItalicStyle";
        case ObliqueStyle:
            return "ObliqueStyle";
        case AnyStyle:
            return "AnyStyle";
        default:
            return "unknown";
    }
}

/*
    External:   Str_to_CompositeOperator
    Purpose:    Validate a composition operator
    Returns:    pointer to the operator as a C string
*/

const char *
Str_to_CompositeOperator(VALUE str)
{
    char *oper;
    int x;

    // The array of valid composition operators
    static const char * const ops[] =
        {
        "Over", "In", "Out", "Atop", "Xor", "Plus", "Minus",
        "Add", "Subtract", "Difference", "Multiply",
        "Bumpmap", "Copy", "CopyRed", "CopyGreen",
        "CopyBlue", "CopyOpacity", "Clear", NULL
        };

    oper = STRING_PTR(str);
    for (x = 0; ops[x]; x++)
    {
        if (strfcmp(oper, ops[x]) == 0)
        {
            return ops[x];
        }
    }

    rb_raise(rb_eArgError, "invalid composition operator: %s", oper);
    return NULL;
}


/*
    External:   write_temp_image
    Purpose:    Write a temporary copy of the image to the IM registry
    Returns:    the "filename" of the registered image
    Notes:      The `tmpnam' argument must point to an char array
                of size MaxTextExtent.
*/
void
write_temp_image(Image *image, char *tmpnam)
{
    long registry_id;

    registry_id = SetMagickRegistry(ImageRegistryType, image, sizeof(Image), &image->exception);
    if (registry_id < 0)
    {
        rb_raise(rb_eRuntimeError, "SetMagickRegistry failed.");
    }
    HANDLE_IMG_ERROR(image)

    sprintf(tmpnam, "mpri:%ld", registry_id);
}

/*
    External:   delete_temp_image
    Purpose:    Delete the temporary image from the registry
    Returns:    void
*/

void
delete_temp_image(char *tmpnam)
{
    long registry_id = -1;

    sscanf(tmpnam, "mpri:%ld", &registry_id);
    if (registry_id >= 0)
    {
        (void) DeleteMagickRegistry(registry_id);
    }
}

/*
    External:   not_implemented
    Purpose:    raise NotImplementedError
    Notes:      Called when a xMagick API is not available. 
                Replaces Ruby's rb_notimplement function.
    Notes:      The MagickPackageName macro is not available
                until 5.5.7. Use MAGICKNAME instead.
*/
void
not_implemented(const char *method)
{
#define Q(N) Q2(N)
#define Q2(N) #N

    rb_raise(rb_eNotImpError, "the %s method is not supported by "
                              Q(MAGICKNAME) " " MagickLibVersionText
                              , method);
}

/*
    Static:     raise_error(msg, loc)
    Purpose:    create a new ImageMagickError object and raise an exception
    Notes:      does not return
                This funky technique allows me to safely add additional
                information to the ImageMagickError object in both 1.6.8 and
                1.8.0. See www.ruby_talk.org/36408.
*/
static void
raise_error(const char *msg, const char *loc)
{
    VALUE exc, mesg, extra;

    mesg = rb_str_new2(msg);
    extra = loc ? rb_str_new2(loc) : Qnil;

    exc = rb_funcall(Class_ImageMagickError, new_ID, 2, mesg, extra);
    rb_funcall(rb_cObject, rb_intern("raise"), 1, exc);
}


/*
    Method:     ImageMagickError#initialize(msg, loc)
    Purpose:    initialize a new ImageMagickError object - store
                the "loc" string in the @magick_location instance variable
*/
VALUE
ImageMagickError_initialize(VALUE self, VALUE mesg, VALUE extra)
{
    VALUE argv[1];

    argv[0] = mesg;
    rb_call_super(1, argv);
    rb_iv_set(self, "@"MAGICK_LOC, extra);

    return self;
}


/*
    Static:     magick_error_handler
    Purpose:    Build error or warning message string. If the error
                is severe, raise the ImageMagickError exception,
                otherwise print an optional warning.
*/
static void
magick_error_handler(
    ExceptionType severity,
    const char *reason,
    const char *description
#if defined(HAVE_EXCEPTIONINFO_MODULE)
    ,
    const char *module,
    const char *function,
    unsigned long line
#endif
    )
{
    char msg[1024];

    if (severity > WarningException)
    {
#if defined(HAVE_SNPRINTF)
        snprintf(msg, sizeof(msg)-1,
#else
        sprintf(msg,
#endif
                     "%s%s%s",
            GET_MSG(severity, reason),
            description ? ": " : "",
            description ? GET_MSG(severity, description) : "");

#if defined(HAVE_EXCEPTIONINFO_MODULE)
        {
        char extra[100];

#if defined(HAVE_SNPRINTF)
        snprintf(extra, sizeof(extra)-1, "%s at %s:%lu", function, module, line);
#else
        sprintf(extra, "%s at %s:%lu", function, module, line);
#endif
        raise_error(msg, extra);
        }
#else
        raise_error(msg, NULL);
#endif
    }
    else if (severity != UndefinedException)
    {
#if defined(HAVE_SNPRINTF)
        snprintf(msg, sizeof(msg)-1,
#else
        sprintf(msg,
#endif
                     "RMagick: %s%s%s",
            GET_MSG(severity, reason),
            description ? ": " : "",
            description ? GET_MSG(severity, description) : "");
        rb_warning(msg);
    }
}


/*
    Extern:     handle_error
    Purpose:    Called from RMagick routines to issue warning messages
                and raise the ImageMagickError exception.
    Notes:      In order to free up memory before calling raise, this
                routine copies the ExceptionInfo data to local storage
                and then calls DestroyExceptionInfo before raising
                the error.

                If the exception is an error, DOES NOT RETURN!
*/
void
handle_error(ExceptionInfo *ex)
{
    ExceptionType sev = ex->severity;
    char reason[251];
    char desc[251];

#if defined(HAVE_EXCEPTIONINFO_MODULE)
    char module[251], function[251];
    unsigned long line;
#endif

    reason[0] = '\0';
    desc[0] = '\0';

    if (sev == UndefinedException)
    {
        return;
    }
    if (ex->reason)
    {
        strncpy(reason, ex->reason, 250);
        reason[250] = '\0';
    }
    if (ex->description)
    {
        strncpy(desc, ex->description, 250);
        desc[250] = '\0';
    }

#if defined(HAVE_EXCEPTIONINFO_MODULE)
    module[0] = '\0';
    function[0] = '\0';

    if (ex->module)
    {
        strncpy(module, ex->module, 250);
        module[250] = '\0';
    }
    if (ex->function)
    {
        strncpy(function, ex->function, 250);
        function[250] = '\0';
    }
    line = ex->line;
#endif

    // Let ImageMagick reclaim its storage
    DestroyExceptionInfo(ex);
    // Reset the severity. If the exception structure is in an
    // Image and this exception is rescued and the Image reused,
    // we need the Image to be pristine!
    ex->severity = UndefinedException;

#if !defined(HAVE_EXCEPTIONINFO_MODULE)
    magick_error_handler(sev, reason, desc);
#else
    magick_error_handler(sev, reason, desc, module, function, line);
#endif
}

/*
    Extern:     handle_all_errors
    Purpose:    Examine all the images in a sequence. If any
                image has an error, raise an exception. Otherwise
                if any image has a warning, issue a warning message.
*/
void handle_all_errors(Image *seq)
{
    Image *badboy = NULL;
    Image *image = seq;

    while (image)
    {
        if (image->exception.severity != UndefinedException)
        {
            // Stop at the 1st image with an error
            if (image->exception.severity > WarningException)
            {
                badboy = image;
                break;
            }
            else if (!badboy)
            {
                badboy = image;
            }
        }
        image = GET_NEXT_IMAGE(image);
    }

    if (badboy)
    {
        if (badboy->exception.severity > WarningException)
        {
            unseq(seq);
        }
        handle_error(&badboy->exception);
    }
}



/*
    Extern:     toseq
    Purpose:    Convert an array of Image *s to an ImageMagick scene
                sequence (i.e. a doubly-linked list of Images)
    Returns:    a pointer to the head of the scene sequence list
*/
Image *
toseq(VALUE imagelist)
{
    long x, len;
    Image *head = NULL;
#ifndef HAVE_APPENDIMAGETOLIST
    Image *tail = NULL;
#endif

    Check_Type(imagelist, T_ARRAY);
    len = rm_imagelist_length(imagelist);
    if (len == 0)
    {
        rb_raise(rb_eArgError, "no images in this image list");
    }

    for (x = 0; x < len; x++)
    {
        Image *image;

        Data_Get_Struct(rb_ary_entry(imagelist, x), Image, image);
#ifdef HAVE_APPENDIMAGETOLIST
        AppendImageToList(&head, image);
#else
        if (!head)
        {
            head = image;
        }
        else
        {
            image->previous = tail;
            tail->next = image;
        }
        tail = image;
#endif
    }

    return head;
}

/*
    Extern:     unseq
    Purpose:    Remove the ImageMagick links between images in an scene
                sequence.
    Notes:      The images remain grouped via the ImageList
*/
void
unseq(Image *image)
{

    if (!image)
    {
        rb_bug("RMagick FATAL: unseq called with NULL argument.");
    }
    while (image)
    {
#if HAVE_REMOVEFIRSTIMAGEFROMLIST
        (void) RemoveFirstImageFromList(&image);
#else
        Image *next;

        next = GET_NEXT_IMAGE(image);
        image->previous = image->next = NULL;
        image = next;
#endif
    }
}
