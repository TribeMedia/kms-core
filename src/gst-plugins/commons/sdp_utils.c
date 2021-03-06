/*
 * (C) Copyright 2013 Kurento (http://kurento.org/)
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 */

#include "sdp_utils.h"
#include <gst/gst.h>
#include <glib.h>
#include <stdlib.h>

#define GST_CAT_DEFAULT sdp_utils
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);
#define GST_DEFAULT_NAME "sdp_utils"

#define SENDONLY_STR  "sendonly"
#define RECVONLY_STR  "recvonly"
#define SENDRECV_STR  "sendrecv"
#define INACTIVE_STR  "inactive"

static gchar *directions[] =
    { SENDONLY_STR, RECVONLY_STR, SENDRECV_STR, INACTIVE_STR, NULL };

#define RTPMAP "rtpmap"

static gchar *rtpmaps[] = {
  "PCMU/8000/1",
  NULL,
  NULL,
  "GSM/8000/1",
  "G723/8000/1",
  "DVI4/8000/1",
  "DVI4/16000/1",
  "LPC/8000/1",
  "PCMA/8000/1",
  "G722/8000/1",
  "L16/44100/2",
  "L16/44100/1",
  "QCELP/8000/1",
  "CN/8000/1",
  "MPA/90000",
  "G728/8000/1",
  "DVI4/11025/1",
  "DVI4/22050/1",
  "G729/8000/1",
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  "CelB/90000",
  "JPEG/90000",
  NULL,
  "nv/90000",
  NULL,
  NULL,
  "H261/90000",
  "MPV/90000",
  "MP2T/90000",
  "H263/90000",
};

gboolean
sdp_utils_attribute_is_direction (const GstSDPAttribute * attr,
    GstSDPDirection * direction)
{
  gint i;

  for (i = 0; directions[i] != NULL; i++) {
    if (g_ascii_strcasecmp (attr->key, directions[i]) == 0) {
      if (direction != NULL) {
        *direction = i;
      }
      return TRUE;
    }
  }

  return FALSE;
}

static gchar *
sdp_media_get_ssrc_str (const GstSDPMedia * media)
{
  gchar *ssrc = NULL;
  const gchar *val;
  GRegex *regex;
  GMatchInfo *match_info = NULL;

  val = gst_sdp_media_get_attribute_val (media, "ssrc");

  if (val == NULL)
    return NULL;

  regex = g_regex_new ("^(?<ssrc>[0-9]+)(.*)?$", 0, 0, NULL);
  g_regex_match (regex, val, 0, &match_info);
  g_regex_unref (regex);

  if (g_match_info_matches (match_info)) {
    ssrc = g_match_info_fetch_named (match_info, "ssrc");
  }
  g_match_info_free (match_info);

  return ssrc;
}

guint
sdp_utils_media_get_ssrc (const GstSDPMedia * media)
{
  gchar *ssrc_str;
  guint ssrc = 0;
  gint64 val;

  ssrc_str = sdp_media_get_ssrc_str (media);
  if (ssrc_str == NULL) {
    return 0;
  }

  val = g_ascii_strtoll (ssrc_str, NULL, 10);
  if (val > G_MAXUINT32) {
    GST_ERROR ("SSRC %" G_GINT64_FORMAT " not valid", val);
  } else {
    ssrc = val;
  }

  g_free (ssrc_str);

  return ssrc;
}

GstSDPDirection
sdp_utils_media_config_get_direction (const GstSDPMedia * media)
{
  GstSDPDirection dir = SENDRECV;

  guint i, len;

  len = gst_sdp_media_attributes_len (media);

  for (i = 0; i < len; i++) {
    const GstSDPAttribute *a;

    a = gst_sdp_media_get_attribute (media, i);

    if (sdp_utils_attribute_is_direction (a, &dir)) {
      break;
    }
  }

  return dir;
}

/**
 * Returns : a string or NULL if any.
 */
const gchar *
sdp_utils_sdp_media_get_rtpmap (const GstSDPMedia * media, const gchar * format)
{
  guint i, attrs_len;
  gchar *rtpmap = NULL;

  attrs_len = gst_sdp_media_attributes_len (media);

  for (i = 0; i < attrs_len && rtpmap == NULL; i++) {
    const GstSDPAttribute *attr = gst_sdp_media_get_attribute (media, i);

    if (g_ascii_strcasecmp (RTPMAP, attr->key) == 0) {
      if (g_str_has_prefix (attr->value, format)) {
        rtpmap = g_strstr_len (attr->value, -1, " ");
        if (rtpmap != NULL)
          rtpmap = rtpmap + 1;
      }
    }
  }

  if (rtpmap == NULL) {
    gint pt;

    for (i = 0; format[i] != '\0'; i++) {
      if (!g_ascii_isdigit (format[i]))
        return NULL;
    }

    pt = atoi (format);
    if (pt > 34)
      return NULL;

    rtpmap = rtpmaps[pt];
  }

  return rtpmap;
}

static gboolean
sdp_utils_add_setup_attribute (const GstSDPAttribute * attr,
    GstSDPAttribute * new_attr)
{
  const gchar *setup;

  /* follow rules defined in RFC4145 */

  if (g_strcmp0 (attr->key, "setup") != 0) {
    GST_WARNING ("%s is not a setup attribute", attr->key);
    return FALSE;
  }

  if (g_strcmp0 (attr->value, "active") == 0) {
    setup = "passive";
  } else if (g_strcmp0 (attr->value, "passive") == 0) {
    setup = "active";
  } else if (g_strcmp0 (attr->value, "actpass") == 0) {
    setup = "active";
  } else {
    setup = "holdconn";
  }

  return gst_sdp_attribute_set (new_attr, attr->key, setup) == GST_SDP_OK;
}

static gboolean
sdp_utils_set_direction_answer (const GstSDPAttribute * attr,
    GstSDPAttribute * new_attr)
{
  const gchar *direction;

  /* rfc3264 6.1 */
  if (g_ascii_strcasecmp (attr->key, SENDONLY_STR) == 0) {
    direction = RECVONLY_STR;
  } else if (g_ascii_strcasecmp (attr->key, RECVONLY_STR) == 0) {
    direction = SENDONLY_STR;
  } else if (g_ascii_strcasecmp (attr->key, SENDRECV_STR) == 0) {
    direction = SENDRECV_STR;
  } else if (g_ascii_strcasecmp (attr->key, INACTIVE_STR) == 0) {
    direction = INACTIVE_STR;
  } else {
    GST_WARNING ("Invalid attribute direction: %s", attr->key);
    return FALSE;
  }

  return gst_sdp_attribute_set (new_attr, direction, "") == GST_SDP_OK;
}

static gboolean
intersect_attribute (const GstSDPAttribute * attr,
    GstSDPIntersectMediaFunc func, gpointer user_data)
{
  const GstSDPAttribute *a;
  GstSDPAttribute new_attr;

  if (g_strcmp0 (attr->key, "setup") == 0) {
    /* follow rules defined in RFC4145 */
    if (!sdp_utils_add_setup_attribute (attr, &new_attr)) {
      GST_WARNING ("Can not set attribute a=%s:%s", attr->key, attr->value);
      return FALSE;
    }
    a = &new_attr;
  } else if (g_strcmp0 (attr->key, "connection") == 0) {
    /* TODO: Implment a mechanism that allows us to know if a */
    /* new connection is gonna be required or an existing one */
    /* can be used. By default we always create a new one. */
    if (gst_sdp_attribute_set (&new_attr, "connection", "new") != GST_SDP_OK) {
      GST_WARNING ("Can not add attribute a=connection:new");
      return FALSE;
    }
    a = &new_attr;
  } else if (sdp_utils_attribute_is_direction (attr, NULL)) {
    if (!sdp_utils_set_direction_answer (attr, &new_attr)) {
      GST_WARNING ("Can not set direction attribute");
      return FALSE;
    }

    a = &new_attr;
  } else {
    a = attr;
  }

  /* No common media attribute. Filter using callback */
  if (func != NULL) {
    func (a, user_data);
  }

  if (a == &new_attr) {
    gst_sdp_attribute_clear (&new_attr);
  }

  return TRUE;
}

gboolean
sdp_utils_intersect_session_attributes (const GstSDPMessage * msg,
    GstSDPIntersectMediaFunc func, gpointer user_data)
{
  guint i, len;

  len = gst_sdp_message_attributes_len (msg);

  for (i = 0; i < len; i++) {
    const GstSDPAttribute *attr;

    attr = gst_sdp_message_get_attribute (msg, i);

    if (!intersect_attribute (attr, func, user_data))
      return FALSE;
  }

  return TRUE;
}

gboolean
sdp_utils_intersect_media_attributes (const GstSDPMedia * offer,
    GstSDPIntersectMediaFunc func, gpointer user_data)
{
  guint i, len;

  len = gst_sdp_media_attributes_len (offer);

  for (i = 0; i < len; i++) {
    const GstSDPAttribute *attr;

    attr = gst_sdp_media_get_attribute (offer, i);

    if (!intersect_attribute (attr, func, user_data)) {
      return FALSE;
    }
  }

  return TRUE;
}

const gchar *
sdp_utils_get_attr_map_value (const GstSDPMedia * media, const gchar * name,
    const gchar * fmt)
{
  const gchar *val = NULL;
  guint i;

  for (i = 0;; i++) {
    gchar **attrs;

    val = gst_sdp_media_get_attribute_val_n (media, name, i);

    if (val == NULL) {
      return NULL;
    }

    attrs = g_strsplit (val, " ", 0);

    if (g_strcmp0 (fmt, attrs[0] /* format */ ) == 0) {
      g_strfreev (attrs);
      return val;
    }

    g_strfreev (attrs);
  }

  return NULL;
}

gboolean
sdp_utils_for_each_media (const GstSDPMessage * msg, GstSDPMediaFunc func,
    gpointer user_data)
{
  guint i, len;

  len = gst_sdp_message_medias_len (msg);
  for (i = 0; i < len; i++) {
    const GstSDPMedia *media = gst_sdp_message_get_media (msg, i);

    if (!func (media, user_data)) {
      /* Do not continue iterating */
      return FALSE;
    }
  }

  return TRUE;
}

gboolean
sdp_utils_is_attribute_in_media (const GstSDPMedia * media,
    const GstSDPAttribute * attr)
{
  guint i, len;

  len = gst_sdp_media_attributes_len (media);

  for (i = 0; i < len; i++) {
    const GstSDPAttribute *a;

    a = gst_sdp_media_get_attribute (media, i);

    if (g_strcmp0 (attr->key, a->key) == 0 &&
        g_strcmp0 (attr->value, a->value) == 0) {
      return TRUE;
    }
  }

  return FALSE;
}

gboolean
sdp_utils_media_is_active (const GstSDPMedia * media, gboolean offerer)
{
  const gchar *attr;

  attr = gst_sdp_media_get_attribute_val_n (media, "setup", 0);
  if (attr == NULL) {
    goto _default;
  }

  if (offerer) {
    if (g_strcmp0 (attr, "active") == 0) {
      GST_DEBUG ("Remote is 'active', so we are 'passive'");
      return FALSE;
    } else if (g_strcmp0 (attr, "passive") == 0) {
      GST_DEBUG ("Remote is 'passive', so we are 'active'");
      return TRUE;
    }
  } else {
    if (g_strcmp0 (attr, "active") == 0) {
      GST_DEBUG ("We are 'active'");
      return TRUE;
    } else if (g_strcmp0 (attr, "passive") == 0) {
      GST_DEBUG ("We are 'passive'");
      return FALSE;
    }
  }

_default:
  GST_DEBUG ("Negotiated SDP is '%s'. %s", attr,
      offerer ? "Local offerer, so 'passive'" : "Remote offerer, so 'active'");

  return !offerer;
}

static void init_debug (void) __attribute__ ((constructor));

static void
init_debug (void)
{
  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, GST_DEFAULT_NAME, 0,
      GST_DEFAULT_NAME);
}
