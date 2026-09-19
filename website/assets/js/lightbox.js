/* SPDX-License-Identifier: MPL-2.0 */
(function () {
  function ready(fn) {
    if (document.readyState === "loading") {
      document.addEventListener("DOMContentLoaded", fn);
    } else {
      fn();
    }
  }

  ready(function () {
    var dialog = document.createElement("dialog");
    dialog.className = "deneb-zoom-dialog";
    dialog.innerHTML =
      '<form method="dialog"><button type="submit" class="deneb-zoom-close">Close</button></form>' +
      '<img alt="">';
    document.body.appendChild(dialog);

    var img = dialog.querySelector("img");

    document.addEventListener("click", function (event) {
      var link = event.target.closest("a.deneb-zoom");
      if (!link) {
        return;
      }
      event.preventDefault();
      var thumb = link.querySelector("img");
      img.src = link.href;
      img.alt = thumb ? thumb.alt : "";
      if (typeof dialog.showModal === "function") {
        dialog.showModal();
      }
    });

    dialog.addEventListener("click", function (event) {
      if (event.target === dialog) {
        dialog.close();
      }
    });
  });
})();
