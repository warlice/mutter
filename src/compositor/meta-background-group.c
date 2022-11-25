/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/**
 * SECTION:meta-background-group
 * @title: MetaBackgroundGroup
 * @short_description: Container for background actors
 *
 * This class is a subclass of ClutterActor with special handling for
 * MetaBackgroundActor/MetaBackgroundGroup when painting children.
 * It makes sure to only draw the parts of the backgrounds not
 * occluded by opaque windows.
 *
 * See #MetaWindowGroup for more information behind the motivation,
 * and details on implementation.
 */

#include "config.h"

#include "meta/meta-background-group.h"

G_DEFINE_TYPE (MetaBackgroundGroup, meta_background_group, CLUTTER_TYPE_ACTOR);

static void
meta_background_group_class_init (MetaBackgroundGroupClass *klass)
{
}

static void
meta_background_group_init (MetaBackgroundGroup *self)
{
}

ClutterActor *
meta_background_group_new (void)
{
  MetaBackgroundGroup *background_group;

  background_group = g_object_new (META_TYPE_BACKGROUND_GROUP, NULL);

  return CLUTTER_ACTOR (background_group);
}
